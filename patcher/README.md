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
| `challenge-key-overflow` | Unbounded public-key / signature length in the S2C challenge reply (`CBaseClientState::ProcessConnectionlessPacket`) writes past two `char[1024]` stack buffers → stack overflow / RCE from a malicious server. | server → client | **Implemented, signature-validated against 2018 `engine.dll`.** |

### Located but not yet wired

These are reverse-engineered and understood; they slot into the same
`IPatch` framework and are tracked for follow-up work:

- **SplitScreen slot OOB** (`CSVCMsg_SplitScreen.slot` used as an unchecked array
  index → virtual call on attacker-influenced memory). Flagship server → client
  RCE from the Secret Club disclosure.
- **MDL `studiohdr2index` OOB write** (`CMDLCache::ReadMDLFile`, in `datacache.dll`)
  — index from a downloaded model used as an unchecked offset for a write.

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

## Verification status

- The signature matches **exactly twice** in the 2018 (`1.36.2.9`) `engine.dll`,
  and both call sites resolve to the same `bf_read::ReadBits`, matching the
  reverse-engineering. The scanner and patch logic are validated against the real
  binary.
- The fix has **not** been exploit-tested against a live malicious server (no
  public PoC was run). It is a structural fix derived from the documented bug;
  treat "protects against the documented vector" as reasoned, not empirically
  proven, until a reproduction is run.
- Only the 2018 build's signature is included so far. Other builds
  (2016 / 2019 / 2020) need their own signatures confirmed before the patch
  reports anything but `NotApplicable` for them.

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

## Installing into a downloaded build

Copy both files into the game folder (next to `csgo.exe`) and start the game with
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
