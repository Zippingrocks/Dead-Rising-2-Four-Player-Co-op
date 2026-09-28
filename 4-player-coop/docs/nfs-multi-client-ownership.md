# Native File-Transfer Ownership

2026-09-27. Narrow four-client campaign-loading investigation. This is not a release or a gameplay pass.

## Reproduction

Native-flow controls `four_instance_20260927_183813` and `184847` retain all four connected meshes without
crashes, but the host waits for joining clients. Player 2 has JIP state 2 and waits on sync point 18
(`LEVEL_COMPLETE`, callback `006BF7B0`). Players 3/4 have JIP state 0 and no world-data buffer.
The host's sync rows record point 18 only from slot 1. PC level loader `0075A576` waits for pending JIP data
before zombie-state application and registering that completion point. Bypassing the point would hide the cause.

The corrected watcher in `184847` captures the native network-file request table:

- Host request slot 0 has already been cleared.
- Host slot 1: data mode, state 6, receiver handle 0, native link ID 2, 15,970 sent bytes, still owning the
  sole outbound transfer. That transfer's native handle is **1**, its local request-table index.
- Host slot 2: data mode, state 3, receiver handle 0, link ID 1, queued without a transfer object.
- Players 3/4: their own request slot 0 and receiver handle 0, state 3, no received world buffer.
- Request capacity is 32. Both transfer arrays have capacity 1. No capacity exhaustion of the request table.

Evidence: `network-snapshot.*.19.json` and final snapshots in that run. Snapshot timestamps and child PIDs are
retained. These fields distinguish local request indices, receiver handles, and native link IDs; none is a
Steam ID or necessarily the visible player number.

## PC Code

- `0087BD30`: request allocation; table +38, count +20, stride 28.
- `00863FB0`: packed request state is bits 2-4 of +24. Direction is bit 8; data mode is low bits value 2.
- `0087BB30`: starts the queued outbound request and puts its local table index into the transfer object.
- `00872C90`: obtains outbound data from the native one-transfer queue. Its caller supplies link argument 0;
  the callee asserts that argument is zero. Do not turn this parameter into a player index.
- `0087EEB9`: the producer calls native `cLinkManager::GetAddrs` (`008639F0`) with a single link ID 0.
  `00851C50` resolves that ID through actual native link objects. The queued request already has its own link
  ID at +1C. Sending every request to the first connection is a two-player assumption.
- `00882FA2`: released NFS data units are tracked by their segment's low-byte receiver handle.
- `00882FEF`: that receiver handle is passed to `00881E10` (`CleanAndTrySend`), which treats it as a local
  request-table index, clears that entry, and advances queued transfers. Concurrent clients may all use receiver
  handle 0 while the host stores their requests at 0, 1, and 2.

The first-client-only success, retained completed local slot 1, and pending third request agree with these two
ownership errors. The controlled fix below still needs live verification; static code alone is not a campaign pass.

## Opt-In Candidate

`-NfsOwnershipProbe` requires four-player mode and `-NativeFlowProbe`. Ordinary solo/two-player launches do not
install these hooks. DLL SHA256:
`1B1B6AB5FEDE971158A8120B21BBFCBF07BC701CBFFC4F4F7684F1F4E133F55D`.
Exact checkpoint: `builds/nfs_ownership_candidate_20260927`.

Three signature-checked original-call wrappers:

1. At `0087EE3B`, preserve the original data retrieval and record the active request's recipient in thread-local
   state only when it produces data. Keep the native queue selector at zero.
2. At `0087EEB9`, resolve the single address through the original GetAddrs using that recorded native link ID.
   No packet contents, sizes, network IDs, or data serialization are changed.
3. At `00882FEF`, resolve the sole active outbound transfer's local request index. Only remap cleanup if its
   completed data request owns that exact transfer and matches the released receiver handle. Call the original
   cleanup routine with the resolved local index, retaining native cleanup and queued-request startup.

The resolver checks the PC object/vtable, 32-slot request bound, both one-transfer capacities, transfer pointer
ownership, data/send mode, state 4 or 6, receiver-handle range, and native link-ID range. Unsupported state retains
the original path. Installation validates every original call before writing and rolls back on failure.
Destination capture is consumed by the immediately following producer address call; it is not a global last-peer
router. No fake readiness, forced world deserialization, manual roster writes, or larger shared queues are used.

Native tests cover index/handle separation, active/completed requests, ownership mismatch, receive/file modes,
unsupported flags, invalid indices, null tables, bad vtables, and recipient/handle bounds. Existing native ABI
suites still pass. Python watcher/report regressions: 65 passed. Candidate builds with pre-existing warnings only.

## First Live Result

Control `four_instance_20260927_185603` completed with all meshes connected, native flow, no premature exits,
and no campaign-entry pass. All four final captures (`coop_capture.*.20.png`) still show loading screens.
The host logs address selection for links 1 and 2 and completion of local request indices 0, 1, and 2, all
carrying receiver handle 0. Its final request and transfer queues are empty. This is progress over `184847`,
where completed local slot 1 held the only outbound transfer and slot 2 could not start.

However, only Player 2 has a complete 15,970-byte JIP buffer and waits at LEVEL_COMPLETE. Players 3/4 still own
pending receive requests with no world buffer. Host-side completion therefore does NOT prove delivery or assembly
by the correct receiver. Do not promote this candidate to a gameplay build. All eight temporary content archives
and the baseline DLL were restored after this control; original artifacts remain intact.

Control `190607` adds bounded original-call tracing at `0088327D -> 00881E80` (StoreInboundData), including the
segment descriptor/receiver handle, byte count, and native acceptance result. The address wrapper now logs the
actual native-resolved 64-bit address and port, not only its requested link ID. No payload or new flow behavior is
changed. Checkpoint `builds/nfs_intake_trace_20260927`, SHA256
`57239CF5C78EC5DF6D23B991A963A57E8B5933CB034155FCD85BC5BE3E6FE775`.
At 19:11:33-34 the live trace confirms that Player 2 receives three metadata descriptors `00000100`, accepting
only the first. Players 3/4 each receive payload sequences 1-15 (`00185800` onward), all rejected, with no sequence-0
metadata intake. The actual resolved addresses match their synthetic Steam identities: link 1 targets Player 3,
link 2 targets Player 4. This rules out payload destination selection in this control.

The defect in the first candidate is its `*data != nullptr` routing condition. PC buffer chopper `0086B050`
returns a valid metadata descriptor at sequence 0 with bit 8 set, zero encoded payload length, and no data pointer
(`0086B06F-0086B0A2`). The native assembler `0086B160` requires that initial sequence and metadata before copying
payload bytes. The candidate redirected payloads but accidentally retained link 0 for all metadata chunks.

Corrected checkpoint `builds/nfs_header_routing_20260927`, DLL SHA256
`B45429ACFA556D357E37EDE38EB292249366B690D0959B7C96905F73BA4763D3`, routes valid header-only metadata as well as
payload. The resolver requires the owned receiver handle, sequence 0 and zero encoded payload length for metadata;
payload requires an actual data pointer and a sufficient supplied size. Empty/uninitialized descriptors retain
the original path. New native regressions cover header-only chunks, nonzero receiver handles, missing payload,
short buffers, wrong handles, malformed metadata, and unsupported owners. All seven native suites and 65 Python
tests pass. The corrected candidate completed `four_instance_20260927_191316` with a 90-second observation.
The reporter also summarizes bounded intake/address/cleanup observations without inferring complete transfers,
packet loss, or campaign play. Five new reporter tests bring Python regressions to 70. Historical reports remain
unchanged; the current candidate's checkpoint preserves its pre-reporting-update tools and native source/hash.

## Corrected Control Result

All three remote clients accept one metadata segment and 15 payload segments, with zero intake rejections.
They each receive 15,967 world-data bytes, register native flow command 3, and apply their buffers at
19:18:49.592-.618, returning from JIP state 2 to 0. Host native transfer starts at 19:18:49.570 with four users.
The previous missing-data hold is resolved in this control without forced readiness or world deserialization.

This is NOT a four-player gameplay pass. Within a second of application, a listener begins shutting down.
Initial captures show Players 2/4 in the safehouse behind a lost-host dialog and the other two transitioning.
Final `coop_capture.*.20.png` images show safehouse geometry in all four processes, but every remote client has
the lost-host dialog. Final mesh states are all 7. The report correctly withholds network/campaign success.
All processes survive to deliberate cleanup; no logged runtime access violations or debugger exceptions occur.
The next boundary is post-StartGame disconnect/transition ownership, not NFS recipient or chunk assembly.

Restoration was verified: all eight archived-content hashes, baseline DLL
`16B3CAA063F6E33ACFD416661B0F8E5CE2364B3BAD270476A96F01277EAED86E`, original save
`A7012AC4EA8841B052CFB9CA9E4055BCBA9FE1C547EF2036C51083208FADAF44`, and original render settings.
No DR2 processes remain. Keep the corrected candidate/checkpoint; do not promote it to ordinary launches.
