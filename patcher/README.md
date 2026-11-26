# CS:GO Legacy Security Patcher

Runtime security patches for the retired CS:GO builds served by the
[CSGO Legacy Downloader](../README.md). These builds predate Valve's fixes for
several publicly documented Source-engine vulnerabilities, so hosting or joining
them is risky. The patcher closes known **server → client** holes at runtime, so
a player does not have to think about it: the launcher injects the patcher before
any engine code runs, the patcher locates each vulnerable function by signature
and neutralises it, then the game starts normally.

Original game files are never modified on disk — all fixes are applied in memory.

## What it currently patches

| ID | Vulnerability | Direction | Status |
|---|---|---|---|
| `challenge-key-overflow` | Unbounded public-key / signature length in the S2C challenge reply (`CBaseClientState::ProcessConnectionlessPacket`) writes past two `char[1024]` stack buffers → stack overflow / RCE from a malicious server. | server → client | **Implemented, signature-validated against the 2016 / 2018 / 2019 / 2020 `engine.dll`.** |
| `splitscreen-slot-oob` | `CSVCMsg_SplitScreen.slot` used as an unchecked array index, then an indirect virtual call through the fetched pointer → reliable RCE from a malicious server (Secret Club disclosure). | server → client | **Implemented, signature-validated against the 2016 / 2018 / 2019 / 2020 `engine.dll`.** |
| `mdl-studiohdr2-oob` | `studiohdr2index` read from a downloaded `.mdl` and used as an unchecked write offset (`CMDLCache::ReadMDLFile` in `datacache.dll`) → out-of-bounds write from a malicious server (Synacktiv disclosure). | server → client | **Implemented, signature-validated against the 2016 / 2018 / 2019 / 2020 `datacache.dll`.** |

### Deliberately deferred

- **Fragment-reassembly info-leak** (`CNetChan::ReadSubChannelData` /
  `CheckReceivingList`, duplicate fragments accepted → uninitialised buffer sent
  back). An information leak, not a memory-corruption primitive on its own.

## How the challenge-key fix works

The two `bf_read::ReadBits(dst, nBits)` call sites that fill the fixed key buffers
are redirected to a guard thunk. The guard rejects any transfer larger than the
buffer (`nBits > 1024 * 8`): on reject it marks the source `bf_read` as overflowed
— so the engine aborts the handshake as an invalid packet — and forwards the read
with a length of zero so nothing is written out of bounds. In-range reads are
forwarded unchanged, so legitimate connects are unaffected (real keys are far
under the cap).

The patch is **self-validating**: it only acts if the signature matches exactly
the expected number of call sites and all of them resolve to the same target. On
any mismatch (wrong build, already patched, unexpected bytes) it changes nothing
and reports `NotApplicable`.

## How the SplitScreen fix works

The `CSVCMsg_SplitScreen` message parser (`ReadFromBuffer`) validates the `type`
field but not `slot`, which the handler then uses as an unchecked array index
before an indirect virtual call. The fix replaces the parser pointer in the
message vtable with a guard that runs the original parser and then rejects a
parse whose decoded `slot` is outside `[0, MAX_SPLITSCREEN_CLIENTS)`. A rejected
parse makes the engine treat the packet as malformed and drop it, so the
vulnerable handler never runs. Only a data pointer in the vtable is changed; no
code is modified, and the guard self-validates that the slot currently holds the
expected parser before swapping.

## How the MDL fix works

The model loader uses `studiohdr2index` (read from the `.mdl`) as a write offset
into the loaded model buffer, checking only that it is non-zero. The fix is a
small trampoline at that decision: it keeps the non-zero test and adds a bounds
check that the write (`index + 0x34`) stays inside the model buffer. An
out-of-range index is routed to the engine's existing "model has no studiohdr2"
rejection path, so the model is discarded instead of corrupting memory. Only the
affected datacache module is touched, and the hook self-validates the stolen
bytes before installing.

## Verification status

All four shipped builds (2016 / 2018 / 2019 / 2020) are validated:

- `challenge-key-overflow`: a per-build candidate signature matches **exactly
  twice** in each `engine.dll`, and both call sites resolve to the same
  `bf_read::ReadBits`. 2018/2019/2020 share one signature; 2016's two key buffers
  sit at a different stack displacement (the `lea` low byte is `0xD4`, not `0x34`),
  so it has its own candidate. Both buffers stay `0x400` apart on every build, so
  the `char[1024]` cap is correct throughout.
- `splitscreen-slot-oob`: a per-build parser prologue matches **exactly once**, and
  the parser pointer is found in the message vtable(s). The field offsets the guard
  reads (`slot` at object+12, has-bits at object+24, bit 2) are identical on all
  four builds; only the prologue shape differs (2016 aligns the stack), so it has
  its own signature.
- `mdl-studiohdr2-oob`: one hook signature matches **exactly once** in every
  `datacache.dll`; the stolen bytes, the write/reject branch targets and the OOB
  store on the write path all match across builds, and the buffer-size argument the
  bounds check reads (`[ebx+4]`, from `mov ebx,[ebp+0x10]`) is the same on all four.
  No per-build signature is needed.
- A single `csgo-patcher.dll` covers every build: each patch tries its candidates
  and applies the one that matches; an unrecognised binary matches none and is
  reported `NotApplicable`, untouched.
- The scanner, guard logic (including bounds edge cases) and patch targeting are
  all validated against the real binaries of all four builds; the guard decision
  logic is unit-tested.
- The fix has **not** been exploit-tested against a live malicious server (no
  public PoC was run). It is a structural fix derived from the documented bug;
  treat "protects against the documented vector" as reasoned, not empirically
  proven, until a reproduction is run.

## Building

32-bit MSVC toolchain required (the guard thunk uses x86 `__declspec(naked)`
inline assembly). From this folder:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

Outputs:

- `build/Release/csgo-patcher.dll` — the patcher
- `build/Release/csgo-patched-launcher.exe` — the injecting launcher

### Releases (CI)

The build is **not** committed to the repository. The
[`Build patcher`](../.github/workflows/build-patcher.yml) workflow compiles both
files and attaches them to a GitHub release when a `patcher-v*` tag is pushed
(e.g. `git tag patcher-v1.0.0 && git push origin patcher-v1.0.0`); it can also be
run manually to build and keep the artifacts on the run without a release.
`download-game.ps1` fetches them from `releases/latest/download/` when they are
not already present next to the script or in a local build, so a plain checkout
still installs the patcher.

## Installing into a downloaded build

`download-game.ps1` does this automatically. To do it by hand: copy both files into
the game folder (next to `csgo.exe`) and start the game with
`csgo-patched-launcher.exe` instead of `csgo.exe`. The launcher forwards any extra
arguments to the game and always adds `-steam -insecure`. It writes a
`csgo-patcher.log` next to the executable recording which patches applied.

## Layout

```
client/src/
  scan/pattern.*        IDA-style signature scanner
  sys/module.*          loaded-module image lookup
  sys/memory.*          scoped page unprotect, code writes, call redirection
  sys/log.*             file + debugger logging
  patch/patch.h         IPatch interface + apply result
  patch/registry.*      patch registry and apply-all
  patches/              one translation unit per concrete fix
  dllmain.cpp           injected entry point (waits for engine.dll, applies)
launcher/src/main.cpp   suspended-start + inject + resume
```
