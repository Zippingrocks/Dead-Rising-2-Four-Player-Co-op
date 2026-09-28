# Clothing Capacity Crash

2026-09-27. Experimental, opt-in clothing-capacity implementation. The campaign-transition work remains
incomplete; independently controlled four-player gameplay is not ready for validation.

## Reproduction

Hidden four-instance control `runtime_logs/coop/four_instance_20260927_171047` uses candidate
`A500950AB33A4BCCE0AB99B84ABC3001C11E01BD982E4C6D99CF567758D7D470`. It differs from the first
mesh-completing candidate by enabling the existing vectored exception reporter in vanilla co-op harness runs.
The reporter continues exception search; it does not recover from or bypass faults.

Host and Players 3/4 fail at 17:15:50 with `0xC0000005`. Each reports:

```text
EIP=00A2B9E0 reading 0000001C
EAX=0 EBX=FFFFFFFF ECX=FFFFFFFF ESI=0 EDI=0 EBP=0
```

Player 2 remains alive until harness cleanup. All four first pass the mesh-connected flow gate. This is not a
stable handshake/campaign pass. Last pre-failure memory sample is above the 2,048 MB commit reserve.
The game reports only 1,101-1,167 MiB of 2,047 MiB virtual address space used in the three failing processes.

## Proven Failure Path

The faulting instruction is `mov esi,[edi+1C]`. Native allocator `00A2B670` initializes EDI to zero and reaches this
instruction through its failed-allocation branch at `00A2B780`. The requested allocation is 0x2C8 bytes, heap ID -1.
This identifies an invalid allocation context, not evidence of physical-RAM or system-commit exhaustion.

Validated direct calls:

```text
004D1070 clothing async-completion routine
  004D1563 call 00A92EB0
    00A92ECE call 00A2BD40 (0x2C8-byte graphics object allocation)
      00A2BD64 call 00A2B670
        00A2B9E0 null read
```

The clothing attribution is supported by the PC routine's embedded source filename at `00C3F698`:
`Common/ai/clothingmanager/clothingmanager.cpp`, and its async-clothing-load assertion strings.
Other stack-address hints in the log are not an unwound call stack.

## Host Dump Evidence

Dump: `debugger.0/deadrising2_pid13464_20260927_171550_519_threadexit_c0000005.dmp` within the run.
The dump has no ExceptionStream and no surviving faulting game thread, but the in-process logger preserved its
registers and stack address. The dump still contains the corresponding stack and clothing object memory.
Extracted evidence: `debugger.0/allocator-analysis.json` and `debugger.0/clothing-memory.json`.

- Global `00DCB0FC` points to `1E926068`; field `+7EB8` points to clothing manager `22CD92F8`.
- Actor mappings at manager `+3CDC + player*4` are valid for slots 0-3.
- Reserved actor count at `+3DC4` is 2.
- Clothing record stride is 0x10C, 13 records per player (player stride 0xD9C).
- Record heap ID is at manager `+38 + (player*13 + record)*10C`.
- Slot 0 heap IDs: `39,33,35,43,37,44,41,32,34,36,38,40,42`.
- Slot 1 heap IDs: `67,61,63,71,65,72,69,60,62,64,66,68,70`.
- All 13 heap IDs for slot 2 are -1; all 13 for slot 3 are -1.
- Completion token `64100000` on the saved stack selects player 2 (bits 25-26), record 0 (low 16 bits).
- The clothing routine places that record's heap ID into the active allocation scope before calling the graphics
  allocator. The saved scope at `00E1263C/00E12658` confirms -1.

## Native Lifecycle Located

- `004AFB50`: initialize clothing records. Walks four sets of 13 records, but only allocates backing slots when the
  player index is below manager `+3DC4` (comparison at `004AFC5C`).
- `00469890`: native clothing-slot allocation. Reads parent heap from manager `+3DD0 + player*4` at `004698B4`.
  Calls `0041E930` for native sizing, `00A2BE10` for memory, and `00A2ACC0` for heap creation.
- `004AFCD0`: change reserved count. Synchronizes rendering, frees existing models and heaps, writes the new count,
  then reinitializes records. It is not an append-only operation and must not be called during pending outfit loads.
- `004D08A0`: clothing game-mode change. Its mode-2 branch explicitly requests two reserved actors at `004D08BB`.
- Constructor region `004EC5C3..004EC641` allocates two 0x1A4000 backing buffers, stores pointers at `+3DC8/+3DCC`
  and parent heap IDs at `+3DD0/+3DD4`. In the host dump the IDs are 30 and 31.
- Fields `+3DD8..+3DE4` already contain live values (40 in this dump). They are not spare parent-heap slots.
- Destructor region `004D079B..004D0804` visits four clothing slot sets, then destroys only those two parent heaps
  and their two buffers.

## Guarded Candidate

Candidate `tools/bin/case-zero-runtime-clothing-capacity/dinput8.dll`, SHA256
`8B8850CA81FBB82CDD05D265F547C7AE1520B18BA34E13505C9103F4E6863A37`, requires harness mode,
four requested players, and `-coopclothingcapacityprobe` (harness switch `-ClothingCapacityProbe`).
Source/binary checkpoint: `4-player-coop/builds/clothing_capacity_candidate_20260927`.

- `runtime/clothing_heap_owner.h` owns two additional 0x1A4000 native parent buffers/heaps per manager,
  without overwriting any native object fields. Allocation failure rolls back the side allocation.
- `004698B4` selects the side-owned parents for slots 2/3 and retains stock parents for slots 0/1.
  Its x86 thunk preserves registers other than the original EDI destination and preserves EFLAGS: the next
  instruction consumes the flags from an earlier native test.
- The two mode-2 reserve call sites `004D08BD` and `0051D1AD` request four slots through the original
  `004AFCD0` implementation. The constructor and solo-mode requests remain unchanged.
- Reserve refuses pending/completed asynchronous loads and insufficient heap-registry capacity. These failures
  terminate only the disposable harness child, with a diagnostic, rather than continuing with invalid heaps.
- `004D07BA` releases the additional parents after the native destructor has already freed all child heaps.
- All four patch sites are signature-checked before any write. A failed patch rolls back earlier writes.

Native tests cover independent owners, repeat acquisition/release, all parent-allocation/creation failure positions,
cleanup ordering, and 2,000 actual x86 thunk register/flags/stack checks. The watcher independently reads all four
sets of 13 heap IDs and the live native heap registry. The report rejects missing/dead/shared player heap IDs and
does not equate clothing readiness with proven campaign gameplay.

Control `four_instance_20260927_172900` installed the hooks in all four processes, but the memory guard aborted
before the reserve/transition path was reached: available commit fell to 377 MB versus the 2,048 MB reserve.
All four children were still alive, with no recorded access violation. All eight content hashes, the baseline
runtime, and the original user save hash were verified after restoration. This is not a clothing integration pass.

Follow-up candidate `tools/bin/case-zero-runtime-clothing-capacity-v2/dinput8.dll`, SHA256
`74FF3DF5F2B325DBC002E13015DDA1A53AF4A2B32FF839F4488C34DE337A95AC`, corrects heap-registry accounting:
one-to-four needs 41 additional slots, not the 28 sufficient for two-to-four. It credits only distinct, live
original record heaps at manager `+40 + record*10C`, which native `0041E830` destroys. Existing side parents
are not double-counted. The expanded native test covers zero/one/two/four-player reservation budgets.
Checkpoint: `4-player-coop/builds/clothing_capacity_v2_candidate_20260927`. Integration retry:
`four_instance_20260927_173745`.

The retry completed its 90-second observation without a crash. All four meshes connected and all four processes
reserved four clothing players, with 52 distinct live child heaps per process and no pending/completed outfit
loads at the final snapshot. Native one-to-four allocation completed on each game thread. The known allocator
fault did not recur. All four nevertheless stayed at a loading screen: the campaign transition is not complete.
Native destructor execution is covered by the x86 fixture, not yet by natural in-game teardown; harness cleanup
deliberately terminates only its own disposable processes.

Do not simply patch the reserve count to four: it would read other live fields as heap IDs. Do not map invalid
heap -1 to the primary heap or another player's clothing heap, because that hides the ownership failure.
Keep solo/two-player unmodified and signature-check every modified PC site. Start with a bounded hidden test,
require valid independent heap IDs for all requested players, then resume native campaign transfer validation.

Both clothing controls were fully restored. The baseline runtime, all eight pre-test archive hashes, and the
original save hash were verified after `173745`. Later transition diagnostics retain this clothing fix.
