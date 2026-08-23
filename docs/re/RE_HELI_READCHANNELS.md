# RE Evidence: Heli::ReadChannels in igi2.exe (Ghidra + r2)

Issue: #60 · Date: 2026-08-22 · Tooling: Ghidra 12.1.2 headless (auto-analysis, x86:LE:32, base 0x400000) + radare2 cross-check

## Target
Local binary: `/Users/haseeb-mir/Documents/Code/Cpp/igi2.exe` (2,887,680 bytes, stripped PE).
This is the same game family open-igi reverse-engineered using igi2.pdb symbols.

## Address reconciliation
- open-igi cites `Heli::ReadChannels (0x431B70)`.
- In this build, `0x431B70` is **not** a function entry (zero incoming references; it is an
  interior `JZ`). The containing function starts at **`0x00431B50`** (body `0x431B50–0x431C0E`),
  i.e. a small layout shift between builds. Ghidra initially had no function there; it was
  recovered by disassembly sweep and function creation, then decompiled.

## Recovered decompilation (FUN_00431b50)

```c
void FUN_00431b50(undefined4 *param_1, undefined4 param_2, int *param_3)
{
  if (DAT_007529a8 != -1) {
    if (*(int *)(*(int *)(DAT_0834e234 + 0x30) + 0xfc) == 0) {
      if (DAT_007529a8 == 0) goto LAB_00431bcc;
      iVar3 = FUN_00420c60();
      uVar1 = *(uint *)(iVar3 + 0x30);
    } else {
      uVar1 = (uint)(DAT_007529a8 == 0);
    }
    if (uVar1 == 0) {
      FUN_004cb4c0();                       // error/abort path
      // write default 6-dword channel block (zeros + &DAT_00685645)
      ...
      return;
    }
  }
LAB_00431bcc:
  if ((param_3 != 0) && (*param_3 == 1)) {  // <-- TICK ONE RESET PATH
    DAT_084b7318 = FUN_0040e340();          //     restore channel block from authored value
  }
  puVar2 = FUN_004cb5a0(DAT_084b7318);      // read live channel data
  for (iVar3 = 6; iVar3 != 0; --iVar3) {    // copy 6 dwords (24 B) to out-param
    *param_1 = *puVar2++; param_1++;
  }
}
```

## Cross-check against open-igi's documented behavior
open-igi `CutsceneRuntime.cs` states (citing 0x431B70):
1. *"Heli::ReadChannels is bidirectional: the tick-zero reset restores live and target
   collective from the authored value"* — **confirmed**: `*param_3 == 1` triggers a restore of
   the cached channel block (`DAT_084b7318 = FUN_0040e340()`) before reading.
2. *"the opening cutscene programme block cancels Original Thrust on tick one"* — consistent
   with the tick-indexed parameter gating the restore.
3. Output is a **6-dword (24-byte) channel block** — matches open-igi's position+orientation
   (or collective/target pair + 4 control floats) interpretation.
4. A global enable/index (`DAT_007529a8`, compared against -1 and 0) gates whether the heli
   channel system is active, with a fallback through `FUN_00420c60` (+0x30 → +0xfc chain),
   matching open-igi's vehicle-registry indirection description.

## Verdict
- **verified**: tick-one/tick-zero bidirectional channel restore semantics; 6-dword channel
  block output; gated activation global. Function exists at `0x431B50` (this build) /
  `0x431B70` (open-igi's build label).
- **inferred (unchanged)**: exact field names inside the 24-byte block (collective vs cyclic
  split) — requires runtime tracing or the pdb-matched binary to pin down.

Editor-side implication (#60): the preview must treat authored collective as *restorable*
(tick-one semantics), not merely a static spin-rate input.

## Addendum: address drift in this build (LOD 0x4CED50)
The same session checked open-igi's `LodModelChain (0x4CED50)` citation: in this local
build `0x4CED50` is interior code (`02 7e 05 ...`), not a function entry — the same
~small layout drift seen at 0x431B70. The Heli function was recoverable by sweep because
its body covered the cited address; the LOD function's cited address does not land in a
recoverable body here. Conclusion: this stripped `igi2.exe` is a *near-but-not-identical*
build to open-igi's pdb-matched reference. Behavior-level verification (Heli tick
semantics) transfers; exact address labels do not. Locating the LOD name-increment loop
in this build requires behavioral search (call-site of model-name load + char increment)
left as follow-up for #62.
