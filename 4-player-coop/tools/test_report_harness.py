import unittest
import json
from pathlib import Path
from tempfile import TemporaryDirectory

from report_harness import analyze_local_control, analyze_phase2_combat, analyze_transition_control, actor_activation_valid, clothing_snapshot_ready, latest_mesh_snapshot, loading_evidence, mesh_snapshot_connected, scan_log, scan_native_flow, scan_native_teardown, scan_nfs_ownership, scan_save_probe, summarize


def endpoint(peer, state, time="12:01:00.000"):
    return f"{time} [1] direct client: endpoint[0]=ABCD vtable=0 peer={peer:016X} previousState=6 state={state} ready=1"


class HarnessEvidenceTests(unittest.TestCase):
    def test_phase2_combat_requires_four_owned_kills_damage_ko_and_teammate_revive(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            pids = [301, 302, 303, 304]

            def snapshot(captured, kills, health=None, ready=None):
                health = health or [400.0] * 4
                ready = ready or [False] * 4
                return [{'pid': pid, 'captured_at': captured, 'local_user': observer,
                         'local_zombie_kills': kills[observer], 'players': [
                             {'slot': slot, 'effective_health': health[slot],
                              'ready_to_revive': ready[slot]} for slot in range(4)]}
                        for observer, pid in enumerate(pids)]

            checkpoints = [
                ('phase2-baseline', '20:00:00', [0, 0, 0, 0], None, None),
                ('phase2-after-player-1-kill', '20:01:00', [1, 0, 0, 0], None, None),
                ('phase2-after-player-2-kill', '20:02:00', [1, 1, 0, 0], None, None),
                ('phase2-after-player-3-kill', '20:03:00', [1, 1, 1, 0], None, None),
                ('phase2-after-player-4-kill', '20:04:00', [1, 1, 1, 1], None, None),
                ('phase2-damage-before', '20:05:00', [1, 1, 1, 1], None, None),
                ('phase2-damage-after', '20:06:00', [1, 1, 1, 1], [300, 400, 400, 400], None),
                ('phase2-ko', '20:07:00', [1, 1, 1, 1], [0, 400, 400, 400], [True, False, False, False]),
                ('phase2-revived', '20:08:00', [1, 1, 1, 1], [100, 400, 400, 400], None),
            ]
            for name, clock, kills, health, ready in checkpoints:
                data = snapshot(f'2026-09-28T{clock}+00:00', kills, health, ready)
                (run / f'combat-{name}.json').write_text(json.dumps(data), encoding='utf-8')
            inputs = []
            for slot in range(4):
                inputs.append({'StartedAt': f'2026-09-28T20:0{slot}:20+00:00',
                               'UpAcknowledgedAt': f'2026-09-28T20:0{slot}:21+00:00',
                               'Instance': slot, 'Pid': pids[slot], 'Completed': True, 'Error': None,
                               'Mouse': {'Buttons': 1},
                               'NativeInput': [{'local_user': slot, 'pid': pids[slot]}]})
            inputs.append({'StartedAt': '2026-09-28T20:07:20+00:00',
                           'UpAcknowledgedAt': '2026-09-28T20:07:21+00:00',
                           'Instance': 1, 'Pid': pids[1], 'Completed': True, 'Error': None,
                           'ScanCode': 18, 'NativeInput': [{'local_user': 1, 'pid': pids[1]}]})
            (run / 'gameplay-input.jsonl').write_text(
                '\n'.join(json.dumps(row) for row in inputs), encoding='utf-8')
            result = analyze_phase2_combat(run, 4, pids)
            self.assertTrue(result['verified'])
            self.assertEqual(result['kill_slots'], [0, 1, 2, 3])
            self.assertEqual(result['damaged_slots'], [0])
            self.assertEqual(result['ko_slots'], [0])
            self.assertEqual(result['revived_slots'], [0])
            inputs[-1]['NativeInput'] = []
            (run / 'gameplay-input.jsonl').write_text(
                '\n'.join(json.dumps(row) for row in inputs), encoding='utf-8')
            self.assertFalse(analyze_phase2_combat(run, 4, pids)['verified'])

    def test_local_control_requires_owned_inputs_and_every_peer_to_observe_each_move(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            pids = [101, 102, 103, 104]
            before = []
            after = []
            for observer, pid in enumerate(pids):
                before.append({'pid': pid, 'captured_at': '2026-09-28T20:00:00+00:00', 'actors': [
                    {'slot': slot, 'user': slot, 'remote_enabled': 0 if slot == observer else 1,
                     'position': [float(slot), 0.0, 0.0],
                     'hidden': False, 'render_hidden': False} for slot in range(4)]})
                after.append({'pid': pid, 'captured_at': '2026-09-28T20:01:00+00:00', 'actors': [
                    {'slot': slot, 'user': slot, 'remote_enabled': 0 if slot == observer else 1,
                     'position': [float(slot), 0.0, 1.0],
                     'hidden': False, 'render_hidden': False} for slot in range(4)]})
            (run / 'campaign-players.1.json').write_text(json.dumps(before), encoding='utf-8')
            (run / 'campaign-players.2.json').write_text(json.dumps(after), encoding='utf-8')
            records = []
            for instance, pid in enumerate(pids):
                records.append(json.dumps({
                    'StartedAt': f'2026-09-28T20:00:{10 + instance:02d}+00:00',
                    'Instance': instance, 'Pid': pid, 'ScanCode': 17, 'Completed': True, 'Error': None,
                    'UpAcknowledgedAt': f'2026-09-28T20:00:{11 + instance:02d}+00:00',
                    'NativeInput': [{'local_user': instance, 'pid': pid,
                                     'actor': '0x1234', 'scene': '0x5678'}],
                }))
            (run / 'gameplay-input.jsonl').write_text('\n'.join(records), encoding='utf-8')
            result = analyze_local_control(run, 4, pids)
            self.assertTrue(result['verified'])
            self.assertEqual(result['replicated_slots'], [0, 1, 2, 3])
            after[2]['actors'][3]['position'] = [3.0, 0.0, 0.1]
            (run / 'campaign-players.2.json').write_text(json.dumps(after), encoding='utf-8')
            result = analyze_local_control(run, 4, pids)
            self.assertFalse(result['verified'])
            self.assertEqual(result['replicated_slots'], [0, 1, 2])

    def test_transition_control_requires_shared_load_and_owned_post_load_movement(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            pids = [201, 202, 203, 204]
            def snapshot(captured_at, offset, movement=0.0):
                return [{'pid': pid, 'captured_at': captured_at, 'actors': [
                    {'slot': slot, 'user': slot, 'remote_enabled': 0 if slot == observer else 1,
                     'position': [offset + float(slot), 0.0, movement],
                     'hidden': False, 'render_hidden': False} for slot in range(4)]}
                    for observer, pid in enumerate(pids)]
            (run / 'transition-before.json').write_text(
                json.dumps(snapshot('2026-09-28T20:00:00+00:00', 0.0)), encoding='utf-8')
            (run / 'transition-after-arrival.json').write_text(
                json.dumps(snapshot('2026-09-28T20:01:00+00:00', 100.0)), encoding='utf-8')
            (run / 'transition-after-control.json').write_text(
                json.dumps(snapshot('2026-09-28T20:02:00+00:00', 100.0, 1.0)), encoding='utf-8')
            records = [json.dumps({
                'StartedAt': f'2026-09-28T20:01:{10 + instance:02d}+00:00',
                'Instance': instance, 'Pid': pid, 'ScanCode': 17, 'Completed': True, 'Error': None,
                'UpAcknowledgedAt': f'2026-09-28T20:01:{11 + instance:02d}+00:00',
                'NativeInput': [{'local_user': instance, 'pid': pid,
                                 'actor': '0x1234', 'scene': '0x5678'}],
            }) for instance, pid in enumerate(pids)]
            (run / 'gameplay-input.jsonl').write_text('\n'.join(records), encoding='utf-8')
            result = analyze_transition_control(run, 4, pids)
            self.assertTrue(result['verified'])
            self.assertEqual(result['transitioned_slots'], [0, 1, 2, 3])
            self.assertEqual(result['controlled_slots'], [0, 1, 2, 3])
            controlled = json.loads((run / 'transition-after-control.json').read_text(encoding='utf-8'))
            controlled[1]['actors'][2]['position'][2] = 0.1
            (run / 'transition-after-control.json').write_text(json.dumps(controlled), encoding='utf-8')
            self.assertFalse(analyze_transition_control(run, 4, pids)['verified'])

    def test_desync_and_queue_abort_are_failures_without_an_exit_record(self):
        for marker in ('native desync assert: caller=004BC96A expression=inventory mismatch',
                       'jip broadcast queue: FAILED reason=capacity',
                       'pause acknowledgement: FAILED context'):
            with self.subTest(marker=marker):
                result = scan_log(['20:00:00.000 [1] ' + marker], 1, 4)
                self.assertEqual(result['network_status'], 'failed')
                self.assertEqual(len(result['faults']), 1)

    def test_shutdown_subtype_is_separate_from_quit_reason(self):
        row = ('20:00:00.125 [12] native connection failure: client=1234abcd '
               'recipient=0110000170000003 flag=0 reason=6')
        result = scan_native_teardown([row.replace('flag=0', 'flag=2')] + [row] * 40)
        self.assertEqual(len(result['shutdown_events']), 32)
        self.assertEqual(result['shutdown_events'][0], {
            'line': 2, 'timestamp': '20:00:00.125', 'client': '1234ABCD',
            'recipient': '0110000170000003', 'flag': 0, 'reason': 6})
        self.assertEqual(result['quit_requests'], [])
        self.assertNotIn('disconnect_cause', result)

    def test_assert_evidence_is_bounded_and_not_executed(self):
        row = ('20:00:00.125 [12] native desync assert: caller=00451234 '
               'expression=users == expected file=engine/source.cpp line=123 thread=12')
        result = scan_native_teardown([row.replace('line=123', 'line=bad'),
                                      row.replace('users == expected', 'x' * 321)] + [row] * 40)
        self.assertEqual(len(result['desync_asserts']), 32)
        self.assertEqual(result['desync_asserts'][0], {
            'line': 3, 'timestamp': '20:00:00.125', 'caller': '00451234',
            'expression': 'users == expected', 'file': 'engine/source.cpp',
            'source_line': 123, 'thread': 12})
        self.assertNotIn('gameplay_verified', result)

    def test_native_quit_reason_is_raw_bounded_evidence(self):
        row = ('20:00:00.125 [12] native quit request: object=1234abcd caller=00401234 '
               'reason=14 stage=3 previousReason=15 deferredReason=-1 thread=12')
        result = scan_native_teardown([row.replace('reason=14', 'reason=unknown')] + [row] * 40)
        self.assertEqual(len(result['quit_requests']), 32)
        self.assertEqual(result['quit_requests'][0], {
            'line': 2, 'timestamp': '20:00:00.125', 'object': '1234ABCD', 'caller': '00401234',
            'reason': 14, 'stage': 3, 'previous_reason': 15, 'deferred_reason': -1, 'thread': 12})
        self.assertEqual(result['calls'], [])
        self.assertNotIn('disconnect_cause', result)

    def test_teardown_absence_is_not_stability_evidence(self):
        result = scan_native_teardown([])
        self.assertFalse(result['installed'])
        self.assertEqual(result['calls'], [])
        self.assertNotIn('gameplay_verified', result)

    def test_teardown_preserves_caller_thread_and_source_line(self):
        result = scan_native_teardown([
            'native teardown: bounded PC client/P2P shutdown tracing installed; original calls preserved',
            '20:00:00.125 [12] native teardown: call=1 operation=client-shutdown '
            'object=1234abcd caller=00870001 thread=12 stackHints=00870001,00401234',
            '20:00:00.126 [12] native teardown: call=2 operation=p2p-shutdown '
            'object=1234ab00 caller=00870002 thread=12 stackHints=',
        ])
        self.assertTrue(result['installed'])
        self.assertEqual(result['calls'][0], {
            'line': 2, 'timestamp': '20:00:00.125', 'call': 1, 'operation': 'client-shutdown',
            'object': '1234ABCD', 'caller': '00870001', 'thread': 12,
            'stack_address_hints': ['00870001', '00401234']})
        self.assertEqual(result['calls'][1]['stack_address_hints'], [])

    def test_teardown_is_bounded_and_rejects_malformed_hints(self):
        row = ('20:00:00.125 [12] native teardown: call=1 operation=client-shutdown '
               'object=1234ABCD caller=00870001 thread=12 stackHints=00870001')
        result = scan_native_teardown([row + ',invalid', row.replace('client-shutdown', 'guessed-reason'),
                                      row + ',00870001' * 12] + [row] * 40)
        self.assertEqual(len(result['calls']), 32)
        self.assertEqual(result['calls'][0]['line'], 4)

    def test_teardown_distinguishes_topology_event_request_and_cleanup(self):
        operations = ['server-handle-quit', 'server-event-16', 'client-event-16',
                      'server-down-request', 'client-shutdown']
        rows = [f'20:00:00.125 [12] native teardown: call={i+1} operation={operation} '
                'object=1234ABCD caller=00870001 thread=12 stackHints='
                for i, operation in enumerate(operations)]
        result = scan_native_teardown(rows)
        self.assertEqual([row['operation'] for row in result['calls']], operations)
        self.assertNotIn('disconnect_cause', result)

    def test_teardown_report_does_not_promote_gameplay(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'coop_net.log').write_text(
                '20:00:00.125 [12] native teardown: call=1 operation=p2p-shutdown '
                'object=1234ABCD caller=00870001 thread=12 stackHints=')
            result = summarize(run, 2)
            self.assertEqual(len(result['instances'][0]['native_teardown']['calls']), 1)
            self.assertFalse(result['four_player_campaign_verified'])

    def test_nfs_absence_is_not_transfer_success(self):
        result = scan_nfs_ownership([])
        self.assertEqual(result['inbound_trace'], [])
        self.assertEqual(result['observed_metadata_accepted'], 0)
        self.assertNotIn('transfer_complete', result)

    def test_nfs_header_and_rejected_payload_are_distinct(self):
        result = scan_nfs_ownership([
            'nfs ownership: inbound call=1 nfs=1000 descriptor=00000100 handle=0 bytes=0 accepted=1',
            'nfs ownership: inbound call=2 nfs=1000 descriptor=00185800 handle=0 bytes=1068 accepted=0',
        ])
        self.assertEqual(result['observed_metadata_accepted'], 1)
        self.assertEqual(result['observed_payload_accepted'], 0)
        self.assertEqual(result['observed_payload_rejected'], 1)
        self.assertEqual(result['inbound_trace'][1]['sequence'], 1)

    def test_nfs_cleanup_keeps_both_ownership_identifiers(self):
        result = scan_nfs_ownership([
            'nfs ownership: completion wireHandle=0 localIndex=2 recipient=1 validated=1',
            'nfs ownership: chunk address link=1 originalLink=0 result=1 peer=0110000170000002 port=0 manager=1000',
        ])
        self.assertEqual(result['completion_trace'][0]['wire_handle'], 0)
        self.assertEqual(result['completion_trace'][0]['local_index'], 2)
        self.assertEqual(result['address_trace'][0]['peer'], '0110000170000002')
        self.assertEqual(result['observed_payload_accepted'], 0)

    def test_nfs_trace_is_bounded_and_ignores_malformed_rows(self):
        row = 'nfs ownership: inbound call=1 nfs=1000 descriptor=00185800 handle=0 bytes=1068 accepted=1'
        result = scan_nfs_ownership(['nfs ownership: inbound call=bad'] + [row] * 100)
        self.assertEqual(len(result['inbound_trace']), 64)
        self.assertEqual(result['inbound_trace'][0]['line'], 2)
        self.assertEqual(result['observed_payload_accepted'], 64)

    def test_nfs_observations_do_not_promote_gameplay(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'coop_net.log').write_text(
                'nfs ownership: inbound call=1 nfs=1000 descriptor=00000100 handle=0 bytes=0 accepted=1')
            result = summarize(run, 2)
            self.assertEqual(result['instances'][0]['nfs_ownership']['observed_metadata_accepted'], 1)
            self.assertFalse(result['four_player_campaign_verified'])

    def test_loading_rejects_historical_active_offset(self):
        self.assertIsNone(loading_evidence(None))
        snapshot = {'client': {'game_active': 1}}
        self.assertIsNone(loading_evidence(snapshot)['game_active'])
        snapshot['client']['game_active_offset'] = '0x8D'
        self.assertEqual(loading_evidence(snapshot)['game_active'], 1)

    def test_loading_reports_callback_ownership_and_pc_point_name(self):
        row = {'point': 12, 'pending': 1, 'callback': '0x007BA480', 'user': '0x1000'}
        result = loading_evidence({'client': {'sync_callbacks': [row, {'point': 13, 'pending': 0}]}})
        self.assertEqual(result['pending_sync'], [dict(row, name='GAMESTATE_START_LEVEL_LOADING')])
        self.assertFalse(result['server_sync_observed'])
        self.assertNotIn('gameplay_verified', result)

    def test_loading_partial_collection_excludes_absent_and_leaving_rows(self):
        rows = [{'slot': i, 'confirmed': int(i != 3), 'leaving': int(i == 2),
                 'sync_received': [int(p == 12 and i == 1) for p in range(19)],
                 'flow_received': [0] * 10} for i in range(4)]
        result = loading_evidence({'server_sync': rows})
        self.assertTrue(result['server_sync_observed'])
        self.assertEqual(result['partial_collections'], [{
            'kind': 'sync', 'point': 12, 'name': 'GAMESTATE_START_LEVEL_LOADING',
            'received_slots': [1], 'not_received_slots': [0]}])

    def test_loading_distinguishes_failed_server_read_from_empty_collections(self):
        result = loading_evidence({'server_sync_error': 'Unsupported native server sync layout'})
        self.assertFalse(result['server_sync_observed'])
        self.assertIn('Unsupported', result['server_sync_error'])
        self.assertEqual(result['partial_collections'], [])

    def test_native_flow_needs_positive_handoff_evidence(self):
        self.assertFalse(scan_native_flow([])['handoff_observed'])
        result = scan_native_flow([
            'native flow control: mesh connected; native online update owns loading; no harness READY/START/transfer/finalize signals',
            'native transition: automatic state transfer enter call=1',
            'native transition: flow registration call=1 command=3 callback=007BB390',
        ])
        self.assertTrue(result['handoff_observed'])
        self.assertEqual(result['forced_signals'], [])
        self.assertEqual(len(result['registration_trace']), 1)

    def test_native_flow_rejects_each_harness_override(self):
        for prefix in ('client flow signal', 'client data transfer', 'host state transfer',
                       'host data transfer', 'client transition'):
            with self.subTest(prefix=prefix):
                line = f'{prefix}: invoking native original function'
                self.assertEqual(scan_native_flow([line])['forced_signals'], [line])

    def test_native_flow_violation_fails_requested_control_only(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'coop_net.log').write_text('client flow signal: invoking native SignalFlowBasic(command=3)')
            self.assertEqual(summarize(run, 2)['instances'][0]['test_status'], 'unproven')
            (run / 'outcome.json').write_text(json.dumps({'Configuration': {'NativeFlowProbe': True}}))
            result = summarize(run, 2)
            self.assertEqual(result['instances'][0]['test_status'], 'failed')
            self.assertFalse(result['native_flow_control_observed'])
            self.assertFalse(result['four_player_campaign_verified'])

    def test_native_flow_traces_are_bounded(self):
        result = scan_native_flow(['native transition: flow registration x'] * 120 +
                                 ['client transition: invoking native x'] * 120)
        self.assertEqual(len(result['registration_trace']), 96)
        self.assertEqual(len(result['forced_signals']), 16)

    def test_clothing_requires_live_independent_storage_for_all_four(self):
        self.assertFalse(clothing_snapshot_ready(None))
        clothing = {'mode': 2, 'reserved_players': 4, 'slots': [
            {'player': player, 'actor': f'0x{player+1:08X}', 'all_heaps_live': True,
             'heap_ids': list(range(player * 13 + 1, player * 13 + 14))} for player in range(4)]}
        snapshot = {'clothing': clothing}
        self.assertTrue(clothing_snapshot_ready(snapshot))
        clothing['reserved_players'] = 2
        self.assertFalse(clothing_snapshot_ready(snapshot))
        clothing['reserved_players'] = 4
        clothing['slots'][3]['all_heaps_live'] = False
        self.assertFalse(clothing_snapshot_ready(snapshot))
        clothing['slots'][3]['all_heaps_live'] = True
        clothing['slots'][3]['heap_ids'] = clothing['slots'][2]['heap_ids'][:]
        self.assertFalse(clothing_snapshot_ready(snapshot))

    def test_mesh_state_requires_count_and_local_identity(self):
        for state, players, local_id, expected in [(2, 4, 2, False), (3, 2, 2, False),
                                                   (3, 4, 0, False), (3, 4, 2, True)]:
            snapshot = {'mesh': {'state': state, 'players': players, 'local_id': local_id}}
            self.assertEqual(mesh_snapshot_connected(snapshot, 2, 4), expected)

    def test_snapshot_selection_uses_timestamp_not_filename_sort(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            for index in (2, 10):
                data = {'pid': 55, 'captured_at': f'2026-09-27T12:00:{index:02d}+00:00', 'mesh': {'state': 2}}
                (run / f'network-snapshot.1.{index}.json').write_text(json.dumps(data), encoding='utf-8')
            snapshot, errors = latest_mesh_snapshot(run, 1, 55)
            self.assertEqual(snapshot['source'], 'network-snapshot.1.10.json')
            self.assertEqual(errors, [])

    def test_snapshot_wrong_process_is_not_evidence(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'network-final.0.json').write_text(
                '{"pid":99,"captured_at":"2026-09-27T12:00:00+00:00"}', encoding='utf-8')
            snapshot, errors = latest_mesh_snapshot(run, 0, 55)
            self.assertIsNone(snapshot)
            self.assertEqual(len(errors), 1)

    def test_alive_without_network_is_unproven(self):
        self.assertEqual(scan_log([], 0, 4)["network_status"], "unproven")

    def test_recovery_does_not_erase_timeout(self):
        lines = ["12:00:00.000 invoking BeginDirectP2PGame",
                 endpoint(0x0110000170000000, 8), endpoint(0x0110000170000000, 6)]
        self.assertEqual(scan_log(lines, 1, 4)["network_status"], "failed")

    def test_host_needs_every_remote(self):
        lines = ["12:00:00.000 invoking BeginDirectP2PGame",
                 endpoint(0x0110000170000001, 6)]
        self.assertEqual(scan_log(lines, 0, 4)["network_status"], "unproven")
        self.assertEqual(scan_log(lines, 0, 2)["network_status"], "connected-at-last-observation")

    def test_midnight_and_no_campaign_claim(self):
        lines = ["23:59:58.000 invoking BeginDirectP2PGame",
                 endpoint(0x0110000170000000, 6, "00:00:02.000")]
        result = scan_log(lines, 1, 4)
        self.assertEqual(result["observed_probe_seconds"], 4)
        self.assertFalse(result["campaign_gameplay_verified"])

    def test_exception_fails_even_when_connected(self):
        lines = ["12:00:00.000 invoking BeginDirectP2PGame",
                 "12:00:01.000 direct p2p: native handoff raised exception",
                 endpoint(0x0110000170000000, 6)]
        self.assertEqual(scan_log(lines, 1, 4)["network_status"], "failed")

    def test_lobby_candidate_requires_matching_live_mode(self):
        mismatch = scan_log([
            "12:00:00.000 loopback lobby: GetLobbyByIndex call=1 index=0 -> 0184000070000001",
            "12:00:00.010 lobby candidate #1 parsed gameMode=0 ranked=0 public=1/3 private=0/0 search=0 expected gameMode=1 ranked=0",
        ], 1, 2)
        self.assertTrue(mismatch['lobby_discovered'])
        self.assertFalse(mismatch['lobby_candidate_compatible'])
        matching = scan_log([
            "12:00:00.010 lobby candidate #1 parsed gameMode=1 ranked=0 public=1/3 private=0/0 search=0 expected gameMode=1 ranked=0",
            "12:00:00.020 loopback lobby: Player 2 joined lobby",
        ], 1, 2)
        self.assertTrue(matching['lobby_candidate_compatible'])
        self.assertTrue(matching['lobby_join_requested'])

    def test_unpack_retry_is_not_a_runtime_fault(self):
        lines = ["11:59:00.000 direct capacity: signature mismatch address=0087F796",
                 "12:00:00.000 invoking BeginDirectP2PGame",
                 endpoint(0x0110000170000000, 6)]
        result = scan_log(lines, 1, 4)
        self.assertEqual(result["network_status"], "connected-at-last-observation")
        self.assertEqual(result["pre_probe_unpack_retries"], 1)

    def test_permanent_unpack_failure_is_a_fault(self):
        result = scan_log(["direct capacity: early layout patches never became ready"], 0, 4)
        self.assertEqual(result["network_status"], "failed")

    def test_process_death_fails_run_without_inventing_network_error(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'resources.csv').write_text('Instance,Alive\n0,true\n0,false\n', encoding='utf-8')
            result = summarize(run, 2)['instances'][0]
            self.assertEqual(result['network_status'], 'unproven')
            self.assertEqual(result['test_status'], 'failed')
            self.assertFalse(result['process_survived_samples'])

    def test_resource_abort_is_preserved(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'outcome.json').write_text('{"Status":"resource-aborted"}', encoding='utf-8-sig')
            result = summarize(run, 4)
            self.assertEqual(result['harness_outcome']['Status'], 'resource-aborted')
            self.assertFalse(result['four_player_campaign_verified'])

    def test_exit_before_first_resource_sample_is_failed(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'outcome.json').write_text(
                '{"Status":"process-exited","Processes":[{"ExitedBeforeCleanup":true,"ExitCode":-1073741819}]}',
                encoding='utf-8')
            result = summarize(run, 2)['instances'][0]
            self.assertEqual(result['test_status'], 'failed')
            self.assertEqual(result['process_exit_code'], -1073741819)

    def test_abnormal_teardown_without_exception_is_failed(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'debugger.0').mkdir()
            (run / 'debugger.0' / 'debug-events.jsonl').write_text(
                '{"eventName":"EXIT_THREAD_DEBUG_EVENT","exitCode":"0xc0000005","abnormalExit":false}\n'
                '{"eventName":"EXIT_PROCESS_DEBUG_EVENT","exitCode":"0xc0000005","abnormalExit":false}\n',
                encoding='utf-8')
            result = summarize(run, 2)['instances'][0]
            self.assertEqual(result['test_status'], 'failed')
            self.assertEqual(result['debugger_abnormal_exit_codes'], ['0xc0000005'])

    def test_nullref_and_repeated_captures_are_not_hidden(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'case_zero_runtime.0.log').write_text(
                'd3d9: harness NULLREF fallback result=00000000\nd3d9: CreateDevice result=00000000\n'
                'd3d9: hidden frame capture D:\\fixture\\coop_capture.0.0.png result=00000000\n'
                'd3d9: hidden frame capture D:\\fixture\\coop_capture.0.1.png result=00000000', encoding='utf-8')
            for index in range(2):
                (run / f'coop_capture.0.{index}.png').write_bytes(b'identical-frame-fixture')
            result = summarize(run, 2)['instances'][0]
            self.assertEqual(result['renderer'], 'nullref')
            self.assertTrue(result['captures_unchanged'])

    def test_failed_or_stale_capture_is_not_current_evidence(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'case_zero_runtime.0.log').write_text(
                'd3d9: hidden frame capture D:\\fixture\\coop_capture.0.0.png result=88760868', encoding='utf-8')
            for index in range(2):
                (run / f'coop_capture.0.{index}.png').write_bytes(b'old-frame')
            result = summarize(run, 2)['instances'][0]
            self.assertEqual(result['captured_frames'], 0)
            self.assertEqual(len(result['unverified_capture_files']), 2)
            self.assertEqual(result['capture_failures'], {'coop_capture.0.0.png': '0x88760868'})

    def test_normal_cleanup_does_not_hide_abnormal_exit(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            debug = run / 'debugger.0'
            debug.mkdir()
            path = debug / 'debug-events.jsonl'
            path.write_text('{"eventName":"EXIT_PROCESS_DEBUG_EVENT","exitCode":"0x00000000"}\n', encoding='utf-8')
            self.assertEqual(summarize(run, 2)['instances'][0]['debugger_abnormal_exit_codes'], [])
            path.write_text('{"eventName":"EXIT_PROCESS_DEBUG_EVENT","exitCode":"0xFFFFFFFF"}\n', encoding='utf-8')
            result = summarize(run, 2)['instances'][0]
            self.assertEqual(result['debugger_abnormal_exit_codes'], ['0xFFFFFFFF'])
            self.assertEqual(result['test_status'], 'failed')

    def test_confirmed_slot_is_not_the_instance_number(self):
        lines = ['12:00:00.000 client table: slot=1 peer=0110000170000003 index=1 confirmed=1']
        result = scan_log(lines, 0, 4)
        self.assertEqual(result['confirmed_slots'], [1])
        self.assertEqual(result['confirmed_peer_ids'], ['0110000170000003'])

    def test_leaving_member_is_not_confirmed_membership(self):
        result = scan_log(['client table: slot=1 peer=0110000170000001 index=1 confirmed=1 leaving=1'], 0, 2)
        self.assertEqual(result['confirmed_peer_ids'], [])

    def test_latest_campaign_actor_snapshot_is_structured(self):
        lines = [
            'campaign actors: game=12340000 scene=23450000 manager=34560000 client=45670000 '
            'localNode=56780000 localSlot=2 singlePlayer=0 gameType=1',
            'campaign actor[0]=11110000 user=0 remoteEnabled=1 vtable=22220000 enableVirtual=33330000',
            'campaign actor[1]=null',
            'campaign actor[2]=33330000 user=2 remoteEnabled=0',
            'campaign actor[3]=null',
        ]
        state = scan_log(lines, 0, 4)['last_campaign_actor_state']
        self.assertEqual(state['local_slot'], 2)
        self.assertEqual(state['game_type'], 1)
        self.assertEqual(state['slots']['0'], {
            'pointer': '11110000', 'user': 0, 'remote_enabled': 1,
            'vtable': '22220000', 'enable_virtual': '33330000'})
        self.assertEqual(state['slots']['1'], {
            'pointer': None, 'user': None, 'remote_enabled': None,
            'vtable': None, 'enable_virtual': None})

    def test_four_player_actor_activation_requires_one_local_and_three_remote(self):
        result = {'last_campaign_actor_state': {
            'local_slot': 2, 'single_player': 0, 'game_type': 1,
            'slots': {
                str(slot): {'pointer': f'{slot + 1:08X}', 'user': slot,
                            'remote_enabled': 0 if slot == 2 else 1}
                for slot in range(4)
            }
        }}
        self.assertTrue(actor_activation_valid(result, 2, 4))
        result['last_campaign_actor_state']['slots']['3']['remote_enabled'] = 0
        self.assertFalse(actor_activation_valid(result, 2, 4))

    def test_save_probe_requires_configuration_identity_and_cleanup(self):
        passed = 'local save probe: PASSED instance=1 roundtrip=1 cleaned=1 cloudWrites=0'
        self.assertEqual(scan_save_probe([passed], 1), 'unproven')
        configured = 'local save storage: configured=1 root=D:\\test'
        self.assertEqual(scan_save_probe([configured, passed], 1), 'passed')
        self.assertEqual(scan_save_probe([configured, passed], 0), 'unproven')
        self.assertEqual(scan_save_probe([configured, passed.replace('cleaned=1', 'cleaned=0')], 1), 'unproven')
        self.assertEqual(scan_save_probe([configured, 'local save probe: FAILED', passed], 1), 'failed')

    def test_save_probe_requires_all_instances_and_empty_fixtures(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'outcome.json').write_text(
                '{"Status":"observation-completed","Configuration":{"SaveProbe":true}}', encoding='utf-8')
            (run / 'resources.csv').write_text('Instance,Alive\n0,true\n1,true\n', encoding='utf-8')
            for instance in range(2):
                (run / f'save.{instance}').mkdir()
                (run / f'case_zero_runtime.{instance}.log').write_text(
                    f'local save storage: configured=1\nlocal save probe: PASSED instance={instance} '
                    'roundtrip=1 cleaned=1 cloudWrites=0\n', encoding='utf-8')
            result = summarize(run, 2)
            self.assertTrue(result['local_save_probe_verified'])
            self.assertFalse(result['native_handshake_observed'])
            self.assertFalse(result['four_player_campaign_verified'])
            (run / 'save.1' / 'sentinel').write_bytes(b'test')
            self.assertFalse(summarize(run, 2)['local_save_probe_verified'])

    def test_logic_only_lobby_join_is_not_a_rendered_frontend_pass(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'outcome.json').write_text(
                '{"Status":"observation-completed","Configuration":{"LobbyProbe":true},'
                '"Processes":[{"ExitedBeforeCleanup":false},{"ExitedBeforeCleanup":false}]}', encoding='utf-8')
            (run / 'resources.csv').write_text('Instance,Alive\n0,true\n1,true\n', encoding='utf-8')
            (run / 'coop_net.log').write_text('', encoding='utf-8')
            (run / 'coop_net.1.log').write_text(
                '12:00:00.000 lobby candidate #1 parsed gameMode=1 ranked=0 public=1/3 private=0/0 '
                'search=0 expected gameMode=1 ranked=0\n'
                '12:00:00.010 loopback lobby: Player 2 joined lobby\n', encoding='utf-8')
            for instance in range(2):
                (run / f'case_zero_runtime.{instance}.log').write_text(
                    'd3d9: harness NULLREF fallback result=00000000\n', encoding='utf-8')
            result = summarize(run, 2)
            self.assertTrue(result['lobby_logic_join_verified'])
            self.assertFalse(result['lobby_frontend_join_verified'])
            for instance in range(2):
                (run / f'case_zero_runtime.{instance}.log').write_text(
                    'd3d9: CreateDevice result=00000000\n', encoding='utf-8')
            self.assertTrue(summarize(run, 2)['lobby_frontend_join_verified'])

    def test_lobby_join_evidence_matches_the_instance(self):
        self.assertFalse(scan_log(['loopback lobby: Player 2 joined lobby'], 2, 4)['lobby_join_requested'])
        self.assertFalse(scan_log(['loopback lobby: Player 3 joined lobby failed'], 2, 4)['lobby_join_requested'])
        self.assertTrue(scan_log(['12:00:00.000 [12] loopback lobby: Player 3 joined lobby'], 2, 4)['lobby_join_requested'])

    def test_frontend_join_requires_every_expected_remote(self):
        for count in (2, 3, 4):
            with self.subTest(instances=count), TemporaryDirectory() as directory:
                run = Path(directory)
                (run / 'outcome.json').write_text(json.dumps({
                    'Status': 'observation-completed', 'Configuration': {'LobbyProbe': True},
                    'Processes': [{'ExitedBeforeCleanup': False} for _ in range(count)],
                }), encoding='utf-8')
                (run / 'resources.csv').write_text(
                    'Instance,Alive\n' + ''.join(f'{i},true\n' for i in range(count)), encoding='utf-8')
                for i in range(count):
                    (run / f'case_zero_runtime.{i}.log').write_text(
                        'd3d9: CreateDevice result=00000000\n', encoding='utf-8')
                for i in range(1, count):
                    partial = summarize(run, count)
                    self.assertFalse(partial['lobby_logic_join_verified'])
                    self.assertFalse(partial['lobby_frontend_join_verified'])
                    self.assertEqual(partial['missing_lobby_join_instances'], list(range(i, count)))
                    (run / f'coop_net.{i}.log').write_text(
                        f'12:00:00.000 loopback lobby: Player {i + 1} joined lobby\n', encoding='utf-8')
                complete = summarize(run, count)
                self.assertTrue(complete['lobby_frontend_join_verified'])
                self.assertEqual(complete['missing_lobby_join_instances'], [])
                self.assertFalse(complete['native_handshake_observed'])
                self.assertFalse(complete['four_player_campaign_verified'])

    def test_stale_endpoint_age_is_visible(self):
        lines = ['12:00:00.000 invoking BeginDirectP2PGame',
                 endpoint(0x0110000170000000, 6), '12:01:20.000 unrelated log']
        result = scan_log(lines, 1, 2)
        self.assertEqual(result['endpoint_observation_age_seconds']['0110000170000000'], 20)

    def test_debugger_unhandled_exception_fails_test(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            debug = run / 'debugger.0'
            debug.mkdir()
            (debug / 'debug-events.jsonl').write_text(
                '{"type":"exception","firstChance":false,"exceptionCode":"0xc0000005","address":"0x1234"}\n',
                encoding='utf-8')
            result = summarize(run, 2)
            self.assertEqual(result['instances'][0]['test_status'], 'failed')
            self.assertFalse(result['native_handshake_observed'])

    def test_runtime_access_violation_is_not_hidden_by_process_survival(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'outcome.json').write_text(
                '{"Status":"observation-completed","Configuration":{"SaveProbe":true}}', encoding='utf-8')
            (run / 'resources.csv').write_text('Instance,Alive\n0,true\n1,true\n', encoding='utf-8')
            for instance in range(2):
                (run / f'save.{instance}').mkdir()
                (run / f'case_zero_runtime.{instance}.log').write_text(
                    f'local save storage: configured=1\nlocal save probe: PASSED instance={instance} '
                    'roundtrip=1 cleaned=1 cloudWrites=0\n', encoding='utf-8')
                name = 'coop_net.log' if instance == 0 else 'coop_net.1.log'
                lines = ['12:00:00.000 invoking BeginDirectP2PGame',
                         endpoint(0x0110000170000000 + (1 - instance), 6)]
                if instance == 0:
                    lines += [f'12:01:00.000 client table: slot={i} peer={0x0110000170000000+i:016X} index={i} confirmed=1'
                              for i in range(2)]
                (run / name).write_text('\n'.join(lines), encoding='utf-8')
            clean = summarize(run, 2)
            self.assertTrue(clean['native_handshake_observed'])
            self.assertTrue(clean['local_save_probe_verified'])
            with (run / 'case_zero_runtime.1.log').open('a', encoding='utf-8') as stream:
                stream.write('12:01:01.000 [42] ACCESS VIOLATION eip=12345678 reading 00000000\n' * 12)
            result = summarize(run, 2)
            self.assertEqual(len(result['instances'][1]['runtime_access_violations']), 8)
            self.assertEqual(result['instances'][1]['test_status'], 'failed')
            self.assertFalse(result['native_handshake_observed'])
            self.assertFalse(result['local_save_probe_verified'])

    def test_debugger_attach_failure_is_not_silent_success(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            debug = run / 'debugger.0'
            debug.mkdir()
            (debug / 'debug-events.jsonl').write_text('{"type":"attach-failed","error":5}\n', encoding='utf-8')
            result = summarize(run, 2)['instances'][0]
            self.assertEqual(result['debugger_status'], 'failed')
            self.assertEqual(result['test_status'], 'failed')

    def test_native_handshake_requires_matching_members_and_fresh_endpoints(self):
        with TemporaryDirectory() as directory:
            run = Path(directory)
            (run / 'outcome.json').write_text('{"Status":"observation-completed"}', encoding='utf-8')
            (run / 'resources.csv').write_text('Instance,Alive\n0,true\n1,true\n', encoding='utf-8')
            for instance in range(2):
                name = 'coop_net.log' if instance == 0 else 'coop_net.1.log'
                lines = ['12:00:00.000 invoking BeginDirectP2PGame',
                         endpoint(0x0110000170000000 + (1 - instance), 6)]
                if instance == 0:
                    lines += [f'12:01:00.000 client table: slot={i} peer={0x0110000170000000+i:016X} index={i} confirmed=1'
                              for i in range(2)]
                (run / name).write_text('\n'.join(lines), encoding='utf-8')
            result = summarize(run, 2)
            self.assertTrue(result['native_handshake_observed'])
            self.assertFalse(result['four_player_campaign_verified'])
            with (run / 'coop_net.log').open('a', encoding='utf-8') as stream:
                stream.write('\n12:01:10.000 unrelated log')
            self.assertFalse(summarize(run, 2)['native_handshake_observed'])


if __name__ == "__main__":
    unittest.main()
