"""Conservative network evidence summary; process survival is a separate gate."""

import argparse
import csv
import json
import hashlib
import math
from datetime import datetime
from pathlib import Path
import re

from analyze_mesh_handshakes import analyze as analyze_mesh_handshakes
from analyze_transition_stall import analyze as analyze_transition_stall
from compare_campaign_combat import compare as compare_combat, load_samples as load_combat_samples


ENDPOINT = re.compile(r"endpoint\[\d+\]=[0-9A-Fa-f]+ .*?peer=([0-9A-Fa-f]{16}) .*? state=(\d+)")
RECORD = re.compile(r"(?:client record|client table:) .*?slot=(\d+) peer=([0-9A-Fa-f]{16}) .*?confirmed=(\d+)")
ACTOR_HEADER = re.compile(
    r"campaign actors: game=([0-9A-Fa-f]+) scene=([0-9A-Fa-f]+) manager=([0-9A-Fa-f]+) "
    r"client=([0-9A-Fa-f]+) localNode=([0-9A-Fa-f]+) localSlot=(-?\d+) singlePlayer=(\d+) gameType=(-?\d+)"
)
ACTOR_SLOT = re.compile(
    r"campaign actor\[(\d+)\]=(null|[0-9A-Fa-f]+)(?: user=(-?\d+) remoteEnabled=(\d+)"
    r"(?: vtable=(null|[0-9A-Fa-f]+) enableVirtual=(null|[0-9A-Fa-f]+))?)?"
)
FAULT = re.compile(r"(?:raised exception|snapshot fault|handoff exception|probe cancelled|signature mismatch|never became ready|native desync assert:|jip broadcast queue: FAILED|pause acknowledgement: FAILED)")
LOBBY_CANDIDATE = re.compile(
    r"lobby candidate #\d+ parsed gameMode=(-?\d+) ranked=(-?\d+) .*?expected gameMode=(-?\d+) ranked=(-?\d+)"
)

# PC table at 0x00D5D568, not OTR's enum ordering.
SYNC_NAMES = (
    'CINEMATIC_RESTORE_STATE', 'GAMESTATE_TRANSITION_BEGIN', 'GAMESTATE_TRANSITION_BEGIN_SHUTDOWN_LEVEL',
    'GAMESTATE_TRANSITION_END', 'GAMESTATE_TRANSITION_END_LAST_STAGE', 'MINIGAMES_START_LOAD',
    'MINIGAMES_DONE_PROPS', 'MINIGAMES_SPLASH_WAIT_TO_EXIT', 'MINIGAMES_READY_FOR_COUNTDOWN',
    'MINIGAMES_READY_FOR_PLAY', 'CONNMESH_CONNECTED', 'GAMESTATE_FINALIZE_START_LEVEL',
    'GAMESTATE_START_LEVEL_LOADING', 'GAMESTATE_START_LEVEL_END_LOADING',
    'GAMESTATE_CINEMATIC_SHUTDOWN_START_DONE', 'GAMESTATE_CINEMATIC_SHUTDOWN_UNLOAD_DONE',
    'SCREEN_CASEFILE_READY_FOR_INPUT', 'SAFEHOUSE_RESCUE_SEQUENCE_DONE', 'LEVEL_COMPLETE',
)


def loading_evidence(snapshot):
    if not snapshot:
        return None
    client = snapshot.get('client') or {}
    corrected = client.get('game_active_offset') == '0x8D'
    server = snapshot.get('server_sync')
    rows = [row for row in (server or []) if row.get('confirmed') == 1 and not row.get('leaving')]
    partial = []
    for kind, key, count in (('flow', 'flow_received', 10), ('sync', 'sync_received', 19)):
        for point in range(count):
            received = [row['slot'] for row in rows if len(row.get(key, [])) == count and row[key][point] == 1]
            not_received = [row['slot'] for row in rows if len(row.get(key, [])) == count and row[key][point] == 0]
            if received and not_received:
                partial.append({'kind': kind, 'point': point,
                                'name': SYNC_NAMES[point] if kind == 'sync' else None,
                                'received_slots': received, 'not_received_slots': not_received})
    return {
        'snapshot_source': snapshot.get('source'), 'captured_at': snapshot.get('captured_at'),
        'game_active': client.get('game_active') if corrected else None,
        'game_active_validated_offset': corrected,
        'loading': snapshot.get('loading'), 'loading_error': snapshot.get('loading_error'),
        'jip_state': client.get('jip_state'), 'jip_bytes': client.get('jip_bytes'),
        'pending_flow': [row for row in client.get('flow_callbacks', []) if row.get('pending')],
        'pending_sync': [dict(row, name=SYNC_NAMES[row['point']] if 0 <= row.get('point', -1) < 19 else None)
                         for row in client.get('sync_callbacks', []) if row.get('pending')],
        'server_sync_observed': server is not None and not snapshot.get('server_sync_error'),
        'server_sync_error': snapshot.get('server_sync_error'), 'partial_collections': partial,
        'limitation': 'One sampled state, not a deadlock or gameplay verdict; native quorum may exclude the host.',
    }


def scan_native_flow(lines):
    forced_markers = (
        'client flow signal: invoking native',
        'client data transfer: invoking native',
        'host state transfer: invoking native',
        'host data transfer: invoking native',
        'client transition: invoking native',
    )
    handoff = False
    forced = []
    registrations = []
    for line in lines:
        handoff |= 'native flow control: mesh connected; native online update owns loading;' in line
        if any(marker in line for marker in forced_markers):
            forced.append(line.strip())
        if 'native transition: flow registration ' in line:
            registrations.append(line.strip())
    return {'handoff_observed': handoff, 'forced_signals': forced[:16],
            'registration_trace': registrations[:96]}


def scan_native_teardown(lines):
    installed = False
    calls = []
    quit_requests = []
    shutdown_events = []
    desync_asserts = []
    shutdown_pattern = re.compile(
        r'^(\d\d:\d\d:\d\d\.\d{3}) \[\d+\] native connection failure: client=([0-9A-Fa-f]{8}) '
        r'recipient=([0-9A-Fa-f]{16}) flag=([01]) reason=(-?\d+)$'
    )
    assert_pattern = re.compile(
        r'^(\d\d:\d\d:\d\d\.\d{3}) \[\d+\] native desync assert: caller=([0-9A-Fa-f]{8}) '
        r'expression=(.{0,320}) file=(.{0,240}) line=(-?\d+) thread=(\d+)$'
    )
    quit_pattern = re.compile(
        r'^(\d\d:\d\d:\d\d\.\d{3}) \[\d+\] native quit request: object=([0-9A-Fa-f]{8}) '
        r'caller=([0-9A-Fa-f]{8}) reason=(-?\d+) stage=(-?\d+) previousReason=(-?\d+) '
        r'deferredReason=(-?\d+) thread=(\d+)$'
    )
    pattern = re.compile(
        r'^(\d\d:\d\d:\d\d\.\d{3}) \[\d+\] native teardown: call=(\d+) '
        r'operation=(client-shutdown|p2p-shutdown|server-down-request|client-event-16|server-event-16|server-handle-quit) object=([0-9A-Fa-f]{8}) '
        r'caller=([0-9A-Fa-f]{8}) thread=(\d+) stackHints=([0-9A-Fa-f,]*)$'
    )
    for number, line in enumerate(lines, 1):
        installed |= 'native teardown: bounded PC client/P2P shutdown tracing installed;' in line
        shutdown_match = shutdown_pattern.fullmatch(line.strip())
        if shutdown_match and len(shutdown_events) < 32:
            timestamp, client, recipient, flag, reason = shutdown_match.groups()
            shutdown_events.append({'line': number, 'timestamp': timestamp, 'client': client.upper(),
                                    'recipient': recipient.upper(), 'flag': int(flag), 'reason': int(reason)})
        assert_match = assert_pattern.fullmatch(line.strip())
        if assert_match and len(desync_asserts) < 32:
            timestamp, caller, expression, file, source_line, thread = assert_match.groups()
            desync_asserts.append({'line': number, 'timestamp': timestamp, 'caller': caller.upper(),
                                  'expression': expression, 'file': file, 'source_line': int(source_line),
                                  'thread': int(thread)})
        quit_match = quit_pattern.fullmatch(line.strip())
        if quit_match and len(quit_requests) < 32:
            timestamp, address, caller, reason, stage, previous, deferred, thread = quit_match.groups()
            quit_requests.append({'line': number, 'timestamp': timestamp, 'object': address.upper(),
                                  'caller': caller.upper(), 'reason': int(reason), 'stage': int(stage),
                                  'previous_reason': int(previous), 'deferred_reason': int(deferred),
                                  'thread': int(thread)})
        match = pattern.fullmatch(line.strip())
        if not match or len(calls) >= 32:
            continue
        timestamp, call, operation, object_address, caller, thread, hints = match.groups()
        frames = hints.split(',') if hints else []
        if len(frames) > 12 or any(not re.fullmatch(r'[0-9A-Fa-f]{8}', frame) for frame in frames):
            continue
        calls.append({'line': number, 'timestamp': timestamp, 'call': int(call), 'operation': operation,
                      'object': object_address.upper(), 'caller': caller.upper(), 'thread': int(thread),
                      'stack_address_hints': [frame.upper() for frame in frames]})
    return {'installed': installed, 'calls': calls, 'quit_requests': quit_requests,
            'shutdown_events': shutdown_events, 'desync_asserts': desync_asserts,
            'limitation': 'Bounded original-call observations; caller addresses and stack hints alone do not establish a disconnect cause or gameplay result.'}


def scan_nfs_ownership(lines):
    inbound, addresses, completions = [], [], []
    for number, line in enumerate(lines, 1):
        match = re.search(r'nfs ownership: inbound call=(\d+) nfs=[0-9A-Fa-f]+ '
                          r'descriptor=([0-9A-Fa-f]{8}) handle=(\d+) bytes=(\d+) accepted=([01])\b', line)
        if match and len(inbound) < 64:
            call, descriptor, handle, size, accepted = match.groups()
            value = int(descriptor, 16)
            inbound.append({'line': number, 'call': int(call), 'descriptor': descriptor.upper(),
                            'handle': int(handle), 'sequence': value >> 20, 'metadata': bool(value & 0x100),
                            'bytes': int(size), 'accepted': accepted == '1'})
        match = re.search(r'nfs ownership: chunk address link=(\d+) originalLink=(\d+) result=([01]) '
                          r'peer=([0-9A-Fa-f]{16}) port=(\d+)', line)
        if match and len(addresses) < 64:
            link, original, resolved, peer, port = match.groups()
            addresses.append({'line': number, 'link': int(link), 'original_link': int(original),
                              'resolved': resolved == '1', 'peer': peer.upper(), 'port': int(port)})
        match = re.search(r'nfs ownership: completion wireHandle=(-?\d+) localIndex=(-?\d+) '
                          r'recipient=(-?\d+) validated=([01])\b', line)
        if match and len(completions) < 32:
            handle, index, recipient, validated = match.groups()
            completions.append({'line': number, 'wire_handle': int(handle), 'local_index': int(index),
                                'recipient': int(recipient), 'validated': validated == '1'})
    return {
        'inbound_trace': inbound, 'address_trace': addresses, 'completion_trace': completions,
        'observed_metadata_accepted': sum(row['metadata'] and row['accepted'] for row in inbound),
        'observed_payload_accepted': sum(not row['metadata'] and row['accepted'] for row in inbound),
        'observed_payload_rejected': sum(not row['metadata'] and not row['accepted'] for row in inbound),
        'limitation': 'Bounded chunk observations only; no packet-loss, complete-transfer, or gameplay verdict.',
    }


def scan_log(lines, instance, instances):
    peers = {}
    peer_times = {}
    faults = []
    endpoint_errors = []
    confirmed = {}
    start = None
    end = None
    day = 0
    last_clock = 0
    unpack_retries = 0
    actor_state = None
    lobby_discovered = False
    lobby_candidate_compatible = False
    lobby_join_requested = False
    for line in lines:
        clock = re.match(r"(\d\d):(\d\d):(\d\d)\.(\d{3})", line)
        if clock:
            h, m, s, ms = map(int, clock.groups())
            value = h * 3600 + m * 60 + s + ms / 1000
            # Trace timestamps can interleave slightly across threads.
            if last_clock - value > 43200:
                day += 86400
            last_clock = value
            end = value + day
        if start is None and ("invoking BeginDirectP2PGame" in line or
                              "lobby frontend: native session observation started" in line):
            start = end
        match = ENDPOINT.search(line)
        if match:
            peer, state = match.groups()
            peers[peer.upper()] = int(state)
            peer_times[peer.upper()] = end
            if int(state) in (8, 9) and len(endpoint_errors) < 8:
                endpoint_errors.append(line.strip())
        match = RECORD.search(line)
        if match:
            leaving = re.search(r'\bleaving=(\d+)', line)
            active = int(match[3]) if not leaving or leaving[1] == '0' else 0
            confirmed[int(match[1])] = (match[2].upper(), active)
        match = ACTOR_HEADER.search(line)
        if match:
            game, scene, manager, client, local_node, local_slot, single_player, game_type = match.groups()
            actor_state = {
                'game': game.upper(), 'scene': scene.upper(), 'actor_manager': manager.upper(),
                'client': client.upper(), 'local_node': local_node.upper(), 'local_slot': int(local_slot),
                'single_player': int(single_player), 'game_type': int(game_type), 'slots': {}
            }
        match = ACTOR_SLOT.search(line)
        if match and actor_state is not None:
            slot, pointer, user, remote, vtable, enable_virtual = match.groups()
            actor_state['slots'][slot] = {
                'pointer': None if pointer == 'null' else pointer.upper(),
                'user': int(user) if user is not None else None,
                'remote_enabled': int(remote) if remote is not None else None,
                'vtable': None if not vtable or vtable == 'null' else vtable.upper(),
                'enable_virtual': None if not enable_virtual or enable_virtual == 'null' else enable_virtual.upper(),
            }
        lobby_discovered |= 'GetLobbyByIndex call=' in line and '-> 0184000070000001' in line
        match = LOBBY_CANDIDATE.search(line)
        if match:
            game_mode, ranked, expected_game_mode, expected_ranked = map(int, match.groups())
            lobby_candidate_compatible |= game_mode == expected_game_mode and ranked == expected_ranked
        lobby_join_requested |= bool(re.search(
            rf'loopback lobby: Player {instance + 1} joined lobby\s*$', line))
        if start is None and "direct capacity: signature mismatch" in line:
            unpack_retries += 1
            continue
        if FAULT.search(line) and len(faults) < 8:
            faults.append(line.strip())
    expected = range(1, instances) if instance == 0 else [0]
    expected_ids = [f"{0x0110000170000000 + peer:016X}" for peer in expected]
    all_connected = all(peers.get(peer) == 6 for peer in expected_ids)
    if endpoint_errors or faults:
        status = "failed"
    elif start is None or not all_connected:
        status = "unproven"
    else:
        status = "connected-at-last-observation"
    return {
        "instance": instance,
        "network_status": status,
        "observed_probe_seconds": round(end - start, 3) if start is not None and end is not None else None,
        "last_endpoint_states": peers,
        "endpoint_errors": endpoint_errors,
        "faults": faults,
        "pre_probe_unpack_retries": unpack_retries,
        "endpoint_observation_age_seconds": {
            peer: round(end - time, 3) if end is not None and time is not None else None
            for peer, time in peer_times.items()
        },
        "confirmed_slots": sorted(slot for slot, (_, value) in confirmed.items() if value == 1),
        "confirmed_peer_ids": sorted(peer for peer, value in confirmed.values() if value == 1),
        "campaign_gameplay_verified": False,
        "last_campaign_actor_state": actor_state,
        "lobby_discovered": lobby_discovered,
        "lobby_candidate_compatible": lobby_candidate_compatible,
        "lobby_join_requested": lobby_join_requested,
    }


def scan_save_probe(lines, instance):
    configured = False
    passed = False
    failed = False
    for line in lines:
        configured |= 'local save storage: configured=1' in line
        if 'local save probe:' not in line:
            continue
        failed |= 'FAILED' in line
        passed |= bool(re.search(
            rf'local save probe: PASSED instance={instance} roundtrip=1 cleaned=1 cloudWrites=0\b', line))
    return 'failed' if failed else 'passed' if configured and passed else 'unproven'


def actor_activation_valid(result, instance, instances):
    state = result.get('last_campaign_actor_state')
    if not state or state.get('local_slot') != instance or state.get('single_player') != 0 or state.get('game_type') != 1:
        return False
    slots = state.get('slots', {})
    if set(slots) != {str(slot) for slot in range(instances)}:
        return False
    return all(
        slots[str(slot)].get('pointer') is not None and
        slots[str(slot)].get('user') == slot and
        slots[str(slot)].get('remote_enabled') == (0 if slot == instance else 1)
        for slot in range(instances)
    )


def _parse_timestamp(value):
    if not value:
        return None
    try:
        return datetime.fromisoformat(str(value).replace('Z', '+00:00'))
    except (TypeError, ValueError):
        return None


def _owned_movement_records(run, instances, pids):
    records = []
    acknowledged = []
    errors = []
    path = run / 'gameplay-input.jsonl'
    if not path.is_file():
        return records, errors
    for number, line in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except (TypeError, ValueError) as error:
            errors.append(f'gameplay input line {number}: {error}')
            continue
        instance = record.get('Instance')
        samples = record.get('NativeInput') or []
        ownership_valid = bool(samples) and all(
            sample.get('local_user') == instance and sample.get('pid') == record.get('Pid') and
            sample.get('actor') not in (None, '0x00000000', '0x0') and
            sample.get('scene') not in (None, '0x00000000', '0x0')
            for sample in samples
        )
        started = _parse_timestamp(record.get('StartedAt'))
        finished = _parse_timestamp(record.get('UpAcknowledgedAt'))
        base_valid = (isinstance(instance, int) and 0 <= instance < instances and
                      record.get('Pid') == pids[instance] and record.get('Completed') is True and
                      not record.get('Error') and record.get('ScanCode') in (17, 30, 31, 32) and
                      started and finished and finished >= started)
        if base_valid:
            acknowledged.append({'instance': instance, 'start': started, 'end': finished})
            if ownership_valid:
                records.append({'instance': instance, 'start': started, 'end': finished,
                                'source': 'native-input'})
    for path in run.glob('navigation_*_p*.json'):
        try:
            navigation = json.loads(path.read_text(encoding='utf-8-sig'))
            instance = navigation.get('Instance')
            steps = navigation.get('Steps') or []
            if (not isinstance(instance, int) or not 0 <= instance < instances or
                    navigation.get('Pid') != pids[instance] or len(steps) < 2):
                continue
            times = []
            for step in steps:
                camera = step.get('camera') or {}
                actor = camera.get('local_actor') or {}
                captured = _parse_timestamp(camera.get('captured_at'))
                if (camera.get('local_user') != instance or camera.get('pid') != pids[instance] or
                        actor.get('user') != instance or actor.get('address') in (None, '0x00000000', '0x0') or
                        camera.get('scene') in (None, '0x00000000', '0x0') or captured is None):
                    raise ValueError('navigation ownership sample does not match harness child')
                times.append(captured)
            matching = [record for record in acknowledged if record['instance'] == instance and
                        record['start'] >= min(times) and record['end'] <= max(times)]
            if matching:
                records.append({'instance': instance, 'start': min(record['start'] for record in matching),
                                'end': max(record['end'] for record in matching),
                                'source': path.name})
        except (OSError, TypeError, ValueError, IndexError) as error:
            errors.append(f'{path.name}: {error}')
    return records, errors


def _campaign_snapshot(path, instances, pids):
    rows = json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(rows, list) or len(rows) != instances:
        raise ValueError('observer count does not match harness instances')
    times = []
    positions = {}
    for observer, row in enumerate(rows):
        if row.get('pid') != pids[observer]:
            raise ValueError(f'observer {observer} PID does not match harness child')
        captured = _parse_timestamp(row.get('captured_at'))
        if captured is None:
            raise ValueError(f'observer {observer} has no valid timestamp')
        times.append(captured)
        actors = {actor.get('slot'): actor for actor in row.get('actors', [])}
        if set(actors) != set(range(instances)):
            raise ValueError(f'observer {observer} does not contain all player slots')
        for slot, actor in actors.items():
            position = actor.get('position')
            if (not isinstance(position, list) or len(position) != 3 or actor.get('user') != slot or
                    actor.get('remote_enabled') != (0 if slot == observer else 1) or
                    actor.get('hidden') is not False or actor.get('render_hidden') is not False):
                raise ValueError(f'observer {observer} slot {slot} is not an active visible actor')
            positions[(observer, slot)] = tuple(float(value) for value in position)
    return {'path': path.name, 'start': min(times), 'end': max(times), 'positions': positions}


def _movement_evidence(before, after, instances, minimum, maximum=None):
    distances = {}
    replicated = []
    for slot in range(instances):
        slot_distances = []
        for observer in range(instances):
            first = before['positions'][(observer, slot)]
            last = after['positions'][(observer, slot)]
            distance = math.sqrt(sum((last[axis] - first[axis]) ** 2 for axis in range(3)))
            distances[f'{observer}:{slot}'] = round(distance, 4)
            slot_distances.append(distance)
        if all(distance >= minimum and (maximum is None or distance <= maximum)
               for distance in slot_distances):
            replicated.append(slot)
    return distances, replicated


def _inputs_between(records, before, after, instances):
    observed = {record['instance'] for record in records
                if record['start'] >= before['end'] and record['end'] <= after['start']}
    return sorted(observed), sorted(set(range(instances)) - observed)


def analyze_local_control(run, instances, pids, movement_threshold=0.4):
    """Correlate owned input with bounded movement replicated in every campaign view."""
    evidence = {'verified': False, 'movement_threshold': movement_threshold, 'input_instances': [],
                'missing_input_instances': list(range(instances)), 'before_snapshot': None,
                'after_snapshot': None, 'replicated_slots': [], 'movement_by_observer': {},
                'ignored_snapshots': [], 'errors': []}
    if instances != 4 or len(pids) != instances or any(pid is None for pid in pids):
        evidence['errors'].append('four harness child PIDs are required')
        return evidence
    records, errors = _owned_movement_records(run, instances, pids)
    evidence['errors'].extend(errors)
    snapshots = []
    explicit = [run / 'local-control-before.json', run / 'local-control-after.json']
    paths = explicit if all(path.is_file() for path in explicit) else list(run.glob('campaign-players.*.json'))
    for path in paths:
        try:
            snapshots.append(_campaign_snapshot(path, instances, pids))
        except (OSError, TypeError, ValueError, IndexError) as error:
            evidence['ignored_snapshots'].append(f'{path.name}: {error}')
    candidates = []
    for before in snapshots:
        for after in snapshots:
            if before['end'] >= after['start']:
                continue
            observed, missing = _inputs_between(records, before, after, instances)
            distances, replicated = _movement_evidence(before, after, instances, movement_threshold, 10.0)
            candidates.append((not missing and replicated == list(range(instances)), len(replicated),
                               after['start'] - before['end'], before, after, observed, missing,
                               distances, replicated))
    if not candidates:
        evidence['errors'].append('campaign snapshots do not bracket four owned inputs')
        return evidence
    candidate = sorted(candidates, key=lambda item: (not item[0], -item[1], item[2]))[0]
    verified, _, _, before, after, observed, missing, distances, replicated = candidate
    evidence.update({'input_instances': observed, 'missing_input_instances': missing,
                     'before_snapshot': before['path'], 'after_snapshot': after['path'],
                     'replicated_slots': replicated, 'movement_by_observer': distances})
    evidence['verified'] = verified and not evidence['errors']
    return evidence


def analyze_transition_control(run, instances, pids, movement_threshold=0.4):
    evidence = {'verified': False, 'before_snapshot': 'transition-before.json',
                'arrival_snapshot': 'transition-after-arrival.json',
                'control_snapshot': 'transition-after-control.json', 'input_instances': [],
                'missing_input_instances': list(range(instances)), 'transitioned_slots': [],
                'controlled_slots': [], 'transition_distance_by_observer': {},
                'movement_by_observer': {}, 'errors': []}
    if instances != 4 or len(pids) != instances or any(pid is None for pid in pids):
        evidence['errors'].append('four harness child PIDs are required')
        return evidence
    try:
        before = _campaign_snapshot(run / evidence['before_snapshot'], instances, pids)
        arrival = _campaign_snapshot(run / evidence['arrival_snapshot'], instances, pids)
        controlled = _campaign_snapshot(run / evidence['control_snapshot'], instances, pids)
    except (OSError, TypeError, ValueError, IndexError) as error:
        evidence['errors'].append(str(error))
        return evidence
    records, errors = _owned_movement_records(run, instances, pids)
    evidence['errors'].extend(errors)
    observed, missing = _inputs_between(records, arrival, controlled, instances)
    transition_distances, transitioned = _movement_evidence(before, arrival, instances, 20.0)
    movement_distances, moved = _movement_evidence(arrival, controlled, instances, movement_threshold, 10.0)
    evidence.update({'input_instances': observed, 'missing_input_instances': missing,
                     'transitioned_slots': transitioned, 'controlled_slots': moved,
                     'transition_distance_by_observer': transition_distances,
                     'movement_by_observer': movement_distances})
    evidence['verified'] = (not evidence['errors'] and not missing and
                            transitioned == list(range(instances)) and moved == list(range(instances)))
    return evidence


def _combat_snapshot_window(path, pids):
    samples = load_combat_samples(path, pids)
    times = []
    for sample in samples.values():
        captured = datetime.fromisoformat(sample['captured_at'])
        if captured.tzinfo is None:
            raise ValueError(f'{path.name}: combat timestamp lacks timezone')
        times.append(captured)
    if (max(times) - min(times)).total_seconds() > 5:
        raise ValueError(f'{path.name}: combat samples are not synchronized')
    return {'path': path, 'start': min(times), 'end': max(times)}


def _owned_action_instances(run, before, after, pids, predicate):
    observed = set()
    path = run / 'gameplay-input.jsonl'
    if not path.is_file():
        return []
    for line in path.read_text(encoding='utf-8-sig', errors='replace').splitlines():
        try:
            record = json.loads(line)
            instance = record['Instance']
            start = datetime.fromisoformat(record['StartedAt'])
            end = datetime.fromisoformat(record['UpAcknowledgedAt'])
            native = record.get('NativeInput', [])
            owned = any(sample.get('local_user') == instance and sample.get('pid') == pids[instance]
                        for sample in native)
            if (record.get('Completed') is True and not record.get('Error') and owned and
                    record.get('Pid') == pids[instance] and before['end'] <= start and
                    end <= after['start'] and predicate(record)):
                observed.add(instance)
        except (json.JSONDecodeError, KeyError, TypeError, ValueError, IndexError):
            continue
    return sorted(observed)


def analyze_phase2_combat(run, instances, pids):
    """Require attributable kills, replicated damage, KO, and an owned teammate revive."""
    evidence = {'verified': False, 'kill_slots': [], 'kill_attack_instances': {},
                'damaged_slots': [], 'ko_slots': [], 'revived_slots': [],
                'revive_interact_instances': [], 'checkpoints': {}, 'errors': []}
    if instances != 4 or len(pids) != instances or any(pid is None for pid in pids):
        evidence['errors'].append('four harness child PIDs are required')
        return evidence
    names = ['phase2-baseline'] + [f'phase2-after-player-{slot + 1}-kill' for slot in range(4)]
    names += ['phase2-damage-before', 'phase2-damage-after', 'phase2-ko', 'phase2-revived']
    windows = {}
    for name in names:
        path = run / f'combat-{name}.json'
        evidence['checkpoints'][name] = path.name
        try:
            windows[name] = _combat_snapshot_window(path, pids)
        except (OSError, KeyError, TypeError, ValueError) as error:
            evidence['errors'].append(str(error))
    if evidence['errors']:
        return evidence
    previous = windows['phase2-baseline']
    for slot in range(4):
        current = windows[f'phase2-after-player-{slot + 1}-kill']
        result = compare_combat(previous['path'], current['path'], pids)
        attacks = _owned_action_instances(
            run, previous, current, pids,
            lambda record: bool(record.get('Mouse', {}).get('Buttons', 0) & 1))
        evidence['kill_attack_instances'][str(slot)] = attacks
        if result['kill_attributed_slots'] == [slot] and slot in attacks:
            evidence['kill_slots'].append(slot)
        previous = current
    damage_before = windows['phase2-damage-before']
    damage_after = windows['phase2-damage-after']
    damage = compare_combat(damage_before['path'], damage_after['path'], pids)
    evidence['damaged_slots'] = damage['damaged_slots']
    ko = compare_combat(damage_after['path'], windows['phase2-ko']['path'], pids)
    evidence['ko_slots'] = ko['ko_slots']
    revived = compare_combat(windows['phase2-ko']['path'], windows['phase2-revived']['path'], pids)
    evidence['revived_slots'] = revived['revived_slots']
    evidence['revive_interact_instances'] = _owned_action_instances(
        run, windows['phase2-ko'], windows['phase2-revived'], pids,
        lambda record: record.get('ScanCode') == 18)
    matching_revival = sorted(set(evidence['ko_slots']).intersection(evidence['revived_slots']))
    teammate_revive = any(instance not in matching_revival
                          for instance in evidence['revive_interact_instances'])
    evidence['verified'] = (evidence['kill_slots'] == [0, 1, 2, 3] and
                            bool(evidence['damaged_slots']) and bool(matching_revival) and
                            teammate_revive)
    return evidence


def latest_mesh_snapshot(run, instance, pid):
    snapshots = []
    errors = []
    paths = list(run.glob(f'network-snapshot.{instance}.*.json'))
    paths += list(run.glob(f'network-final.{instance}.json'))
    for path in paths:
        try:
            data = json.loads(path.read_text(encoding='utf-8-sig'))
            if pid is None or data.get('pid') != pid:
                raise ValueError('snapshot PID does not match the harness child')
            timestamp = datetime.fromisoformat(data['captured_at'])
            if timestamp.tzinfo is None:
                raise ValueError('snapshot timestamp lacks timezone')
            snapshots.append((timestamp, path.name, data))
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append(f'{path.name}: {error}')
    if not snapshots:
        return None, errors
    _, source, data = max(snapshots, key=lambda entry: entry[0])
    return dict(data, source=source), errors


def mesh_snapshot_connected(snapshot, instance, instances):
    mesh = snapshot.get('mesh') if snapshot else None
    return bool(mesh and mesh.get('state') == 3 and mesh.get('players') == instances and
                mesh.get('local_id') == instance)


def clothing_snapshot_ready(snapshot):
    clothing = snapshot.get('clothing') if snapshot else None
    if not clothing or clothing.get('reserved_players') != 4 or clothing.get('mode') != 2:
        return False
    slots = clothing.get('slots', [])
    if len(slots) != 4 or {slot.get('player') for slot in slots} != {0, 1, 2, 3}:
        return False
    used = set()
    for slot in slots:
        ids = slot.get('heap_ids', [])
        if (not slot.get('all_heaps_live') or slot.get('actor') in (None, '0x00000000') or
                len(ids) != 13 or any(not isinstance(value, int) or not 0 < value < 128 for value in ids)):
            return False
        if used.intersection(ids):
            return False
        used.update(ids)
    return True


def summarize(run, instances):
    resources = run / "resources.csv"
    rows = []
    if resources.exists():
        with resources.open(encoding="utf-8-sig") as stream:
            rows = list(csv.DictReader(stream))
    outcome_path = run / "outcome.json"
    outcome = json.loads(outcome_path.read_text(encoding="utf-8-sig")) if outcome_path.exists() else None
    native_flow_requested = bool(outcome and outcome.get('Configuration', {}).get('NativeFlowProbe'))
    results = []
    for instance in range(instances):
        name = "coop_net.log" if instance == 0 else f"coop_net.{instance}.log"
        path = run / name
        if path.exists():
            with path.open(encoding="utf-8-sig", errors="replace") as stream:
                result = scan_log(stream, instance, instances)
        else:
            result = scan_log([], instance, instances)
        trace_lines = path.read_text(encoding='utf-8-sig', errors='replace').splitlines() if path.exists() else []
        result['native_flow'] = scan_native_flow(trace_lines)
        result['native_teardown'] = scan_native_teardown(trace_lines)
        result['nfs_ownership'] = scan_nfs_ownership(trace_lines)
        samples = [row for row in rows if int(row["Instance"]) == instance]
        result["process_survived_samples"] = bool(samples) and all(row["Alive"].lower() == "true" for row in samples)
        result["test_status"] = (
            "failed" if result["network_status"] == "failed" or
            any(row["Alive"].lower() != "true" for row in samples) else "unproven"
        )
        if native_flow_requested and result['native_flow']['forced_signals']:
            result['test_status'] = 'failed'
        processes = outcome.get('Processes', []) if outcome else []
        process = processes[instance] if instance < len(processes) else {}
        snapshot, snapshot_errors = latest_mesh_snapshot(run, instance, process.get('Pid'))
        result['last_network_snapshot'] = snapshot
        result['loading_evidence'] = loading_evidence(snapshot)
        result['network_snapshot_errors'] = snapshot_errors
        result['native_mesh_connected_at_snapshot'] = mesh_snapshot_connected(snapshot, instance, instances)
        result['four_player_clothing_at_snapshot'] = clothing_snapshot_ready(snapshot)
        result['process_exit_before_cleanup'] = process.get('ExitedBeforeCleanup')
        result['process_exit_code'] = process.get('ExitCode')
        if result['process_exit_before_cleanup']:
            result['test_status'] = 'failed'
        debugger = run / f'debugger.{instance}' / 'debug-events.jsonl'
        result['debugger_unhandled_exceptions'] = []
        result['debugger_abnormal_exit_codes'] = []
        result['debugger_status'] = 'not-recorded'
        if debugger.exists():
            attached = False
            debugger_failed = False
            with debugger.open(encoding='utf-8-sig') as stream:
                for line in stream:
                    event = json.loads(line)
                    attached |= event.get('eventName') == 'CREATE_PROCESS_DEBUG_EVENT'
                    debugger_failed |= event.get('type') in ('attach-failed', 'wait-failed', 'continue-failed')
                    if event.get('eventName') in ('EXIT_THREAD_DEBUG_EVENT', 'EXIT_PROCESS_DEBUG_EVENT'):
                        code = event.get('exitCode')
                        if code is not None and int(code, 0) >= 0x80000000 and code not in result['debugger_abnormal_exit_codes']:
                            result['debugger_abnormal_exit_codes'].append(code)
                    if event.get('type') == 'exception' and event.get('firstChance') is False:
                        result['debugger_unhandled_exceptions'].append({
                            key: event.get(key) for key in ('exceptionCode', 'address', 'dumped', 'dumpPath')
                        })
            result['debugger_status'] = 'failed' if debugger_failed else 'attached' if attached else 'unproven'
            if result['debugger_unhandled_exceptions'] or result['debugger_abnormal_exit_codes'] or debugger_failed:
                result['test_status'] = 'failed'
        runtime_log = run / f'case_zero_runtime.{instance}.log'
        lines = runtime_log.read_text(encoding='utf-8-sig', errors='replace').splitlines() if runtime_log.exists() else []
        result['local_save_probe'] = scan_save_probe(lines, instance)
        result['runtime_access_violations'] = [line.strip() for line in lines if 'ACCESS VIOLATION eip=' in line][:8]
        if result['runtime_access_violations']:
            result['test_status'] = 'failed'
        runtime_text = '\n'.join(lines)
        result['observed_level_enums'] = [int(value) for value in re.findall(r'engine state: level enum=(\d+)', runtime_text)]
        result['renderer'] = (
            'hardware-unavailable' if 'harness renderer: hardware unavailable' in runtime_text else
            'nullref' if 'NULLREF fallback result=00000000' in runtime_text else
            'device-created' if 'd3d9: CreateDevice result=00000000' in runtime_text else 'unproven'
        )
        capture_results = {}
        for line in lines:
            match = re.search(r'hidden frame capture .*?([\\/])?(coop_capture\.\d+\.\d+\.png) result=([0-9A-Fa-f]{8})', line)
            if match:
                capture_results[match[2]] = int(match[3], 16)
        candidates = list(run.glob(f'coop_capture.{instance}.*.png'))
        captures = [path for path in candidates if capture_results.get(path.name) == 0]
        result['unverified_capture_files'] = [path.name for path in candidates if path not in captures]
        result['capture_failures'] = {name: f'0x{code:08X}' for name, code in capture_results.items() if code != 0}
        hashes = {hashlib.sha256(path.read_bytes()).hexdigest() for path in captures}
        result['captured_frames'] = len(captures)
        result['distinct_captured_frames'] = len(hashes)
        result['captures_unchanged'] = len(captures) > 1 and len(hashes) == 1
        save_root = run / f'save.{instance}'
        result['save_fixture_clean'] = save_root.is_dir() and not any(save_root.iterdir())
        if result['local_save_probe'] == 'failed':
            result['test_status'] = 'failed'
        results.append(result)
    expected_ids = {f"{0x0110000170000000 + peer:016X}" for peer in range(instances)}
    debugger_required = bool(outcome and outcome.get('Configuration', {}).get('CrashMonitor'))
    handshake = bool(outcome and outcome.get('Status') == 'observation-completed') and all(
        result['network_status'] == 'connected-at-last-observation' and result['process_survived_samples'] and
        not result['debugger_unhandled_exceptions'] and not result['debugger_abnormal_exit_codes'] and
        not result['runtime_access_violations'] and
        not result['process_exit_before_cleanup'] and
        result['debugger_status'] != 'failed' and
        (not debugger_required or result['debugger_status'] == 'attached') and
        all(age is not None and 0 <= age <= 3 for age in result['endpoint_observation_age_seconds'].values())
        for result in results
    ) and set(results[0]['confirmed_peer_ids']) == expected_ids
    save_verified = bool(outcome and outcome.get('Status') == 'observation-completed' and
                         outcome.get('Configuration', {}).get('SaveProbe')) and all(
        result['local_save_probe'] == 'passed' and result['save_fixture_clean'] and
        result['process_survived_samples'] and not result['debugger_unhandled_exceptions'] and
        not result['runtime_access_violations'] and
        not result['debugger_abnormal_exit_codes'] and not result['process_exit_before_cleanup'] and
        result['debugger_status'] != 'failed' and
        (not debugger_required or result['debugger_status'] == 'attached') for result in results
    )
    actor_activation_verified = bool(
        instances == 4 and handshake and outcome and
        outcome.get('Configuration', {}).get('ActorActivationProbe') and
        all(actor_activation_valid(result, instance, instances) for instance, result in enumerate(results))
    )
    process_rows = (outcome or {}).get('Processes', [])
    pids = [row.get('Pid') for row in process_rows[:instances]]
    local_control = analyze_local_control(run, instances, pids)
    transition_control = analyze_transition_control(run, instances, pids)
    phase2_combat = analyze_phase2_combat(run, instances, pids)
    transition_stall = analyze_transition_stall(run)
    missing_lobby_join_instances = [result['instance'] for result in results[1:]
                                    if not result['lobby_join_requested']]
    lobby_logic_join_verified = bool(
        outcome and outcome.get('Status') == 'observation-completed' and
        outcome.get('Configuration', {}).get('LobbyProbe') and
        not missing_lobby_join_instances and
        all(result['process_survived_samples'] and not result['process_exit_before_cleanup'] and
            result['test_status'] != 'failed' for result in results)
    )
    lobby_join_verified = lobby_logic_join_verified and all(result['renderer'] == 'device-created' for result in results)
    return {"run": str(run.resolve()), "harness_outcome": outcome, "instances": results,
            "native_flow_control_observed": native_flow_requested and handshake and all(
                result['native_flow']['handoff_observed'] and not result['native_flow']['forced_signals']
                for result in results),
            "mesh_handshake_trace": analyze_mesh_handshakes(run, instances),
            "native_handshake_observed": handshake,
            "all_native_meshes_connected_observed": handshake and all(
                result['native_mesh_connected_at_snapshot'] for result in results),
            "four_player_clothing_capacity_observed": instances == 4 and handshake and all(
                result['four_player_clothing_at_snapshot'] for result in results),
            "local_save_probe_verified": save_verified,
            "lobby_logic_join_verified": lobby_logic_join_verified,
            "missing_lobby_join_instances": missing_lobby_join_instances,
            "lobby_frontend_join_verified": lobby_join_verified,
            "four_player_actor_activation_verified": actor_activation_verified,
            "four_player_local_control_verified": actor_activation_verified and local_control['verified'],
            "four_player_local_control_evidence": local_control,
            "four_player_post_transition_control_verified": actor_activation_verified and transition_control['verified'],
            "four_player_post_transition_control_evidence": transition_control,
            "four_player_phase2_combat_verified": actor_activation_verified and phase2_combat['verified'],
            "four_player_phase2_combat_evidence": phase2_combat,
            "transition_stall_analysis": transition_stall,
            "four_player_campaign_verified": actor_activation_verified and local_control['verified'] and transition_control['verified'],
            "four_player_campaign_combat_verified": (actor_activation_verified and local_control['verified'] and
                                                       transition_control['verified'] and phase2_combat['verified'])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--instances", type=int, choices=(2, 3, 4), default=4)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    report = summarize(args.run, args.instances)
    text = json.dumps(report, indent=2)
    if args.write:
        (args.run / "network-summary.json").write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
