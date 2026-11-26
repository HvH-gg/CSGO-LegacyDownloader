#include "patches/mdl_studiohdr2_oob.h"

#include "scan/pattern.h"
#include "sys/memory.h"
#include "sys/module.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace csgopatch {
namespace {

// ---------------------------------------------------------------------------
//  Target  (CS:GO 2018 build 1.36.2.9, datacache.dll, CMDLCache::ReadMDLFile)
// ---------------------------------------------------------------------------
//  edi = pStudioHdr (loaded .mdl buffer base), ebx = &{buffer, size} (size at
//  [ebx+4]), esi = model name, ecx = studiohdr2index.
//
//    .text:A3B7  8B 8F 90 01 00 00   mov  ecx, [edi+190h]   ; studiohdr2index
//    .text:A3BD  85 C9               test ecx, ecx          ; <-- hook here
//    .text:A3BF  75 1A               jnz  A3DB              ; nonzero -> write path
//    .text:A3C1  56                  push esi               ; (no studiohdr2 path)
//    .text:A3C2  68 ..               push offset "...studiohdr2..."
//                ...                 DevWarning(...); return 0
//    .text:A3DB  0F B7 45 08         movzx eax, [ebp+arg_0] ; handle  (write path)
//    .text:A3DF  89 44 39 30         mov  [ecx+edi+30h], eax ; <-- the OOB write
//
//  The index is only checked for non-zero, so `ecx+0x30` can land anywhere in
//  the process. We replace the non-zero test with a trampoline that also
//  bounds-checks the index against the buffer size, and routes an out-of-range
//  index to the existing rejection path instead of the write.
// ---------------------------------------------------------------------------

// mov ecx,[edi+190h] ; test ecx,ecx ; jnz +0x1a ; push esi  -- locates the hook.
constexpr std::string_view kHookSignature = "8B 8F 90 01 00 00 85 C9 75 1A 56";
constexpr std::ptrdiff_t   kHookOffset = 6;   // the `test ecx,ecx` within the match

// Hook-relative addresses of the two branch destinations.
constexpr std::ptrdiff_t kWritePathOffset = 0x1E;  // movzx eax,[ebp+arg_0] (write path)
constexpr std::ptrdiff_t kMissPathOffset  = 0x05;  // push offset "..." (rejection path)

// Bytes stolen by the 5-byte relative jump: test ecx,ecx ; jnz ; push esi.
constexpr std::uint8_t kStolen[]   = {0x85, 0xC9, 0x75, 0x1A, 0x56};
constexpr std::size_t  kStolenSize = sizeof(kStolen);

// Index + 0x30 .. +0x34 must lie inside the buffer (a 4-byte write at +0x30).
constexpr std::uint8_t kWriteEndSlack = 0x34;

// Trampoline: validate `ecx` (studiohdr2index) against the model buffer size in
// [ebx+4]; jump to the write path when in range, otherwise to the rejection
// path (re-running the stolen `push esi`). Branch targets are filled at runtime.
//                                                     offset
constexpr std::uint8_t kStubTemplate[] = {
    0x85, 0xC9,                          // 0  test ecx, ecx
    0x74, 0x15,                          // 2  jz   miss (+0x15)
    0x8B, 0x43, 0x04,                    // 4  mov  eax, [ebx+4]      ; buffer size
    0x2D, 0x34, 0x00, 0x00, 0x00,        // 7  sub  eax, 0x34
    0x72, 0x0B,                          // 12 jb   miss             ; size < 0x34
    0x3B, 0xC8,                          // 14 cmp  ecx, eax
    0x77, 0x07,                          // 16 ja   miss             ; index too large
    0xB8, 0x00, 0x00, 0x00, 0x00,        // 18 mov  eax, <writePath>
    0xFF, 0xE0,                          // 23 jmp  eax
    0x56,                                // 25 miss: push esi         (stolen)
    0xB8, 0x00, 0x00, 0x00, 0x00,        // 26 mov  eax, <missPath>
    0xFF, 0xE0,                          // 31 jmp  eax
};
constexpr std::size_t kWriteTargetSlot = 19;
constexpr std::size_t kMissTargetSlot  = 27;

constexpr wchar_t kModule[]  = L"datacache.dll";
constexpr unsigned kModuleWaitMs = 30'000;

class MdlStudioHdr2OobPatch final : public IPatch {
public:
    std::string_view id() const override { return "mdl-studiohdr2-oob"; }

    std::string_view description() const override {
        return "MDL studiohdr2index used as an unchecked write offset -> server "
               "-> client OOB write (CMDLCache::ReadMDLFile)";
    }

    ApplyResult apply() override {
        if (!wait_for_module(kModule, kModuleWaitMs)) {
            return ApplyResult::not_applicable("datacache.dll not loaded");
        }
        const CodeSpan mod = module_image(kModule);
        if (mod.empty()) return ApplyResult::not_applicable("datacache.dll span unavailable");

        const auto pattern = Pattern::compile(kHookSignature);
        if (!pattern) return ApplyResult::failed("internal: hook signature failed to compile");

        const std::vector<std::uint8_t*> matches = pattern->find_all(mod);
        if (matches.size() != 1) {
            char buf[80];
            std::snprintf(buf, sizeof(buf), "expected 1 hook site, found %zu", matches.size());
            return ApplyResult::not_applicable(buf);
        }

        std::uint8_t* hook = matches.front() + kHookOffset;
        if (*hook == 0xE9) return ApplyResult::already("hook already installed");
        if (std::memcmp(hook, kStolen, kStolenSize) != 0) {
            return ApplyResult::not_applicable("unexpected bytes at hook site");
        }

        auto* write_path = hook + kWritePathOffset;
        auto* miss_path  = hook + kMissPathOffset;

        // Build the trampoline in freshly allocated executable memory.
        auto* stub = static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, sizeof(kStubTemplate), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (stub == nullptr) return ApplyResult::failed("trampoline allocation failed");
        std::memcpy(stub, kStubTemplate, sizeof(kStubTemplate));
        std::memcpy(stub + kWriteTargetSlot, &write_path, sizeof(write_path));
        std::memcpy(stub + kMissTargetSlot, &miss_path, sizeof(miss_path));
        FlushInstructionCache(GetCurrentProcess(), stub, sizeof(kStubTemplate));

        // Redirect the hook site to the trampoline (E9 rel32 over the stolen bytes).
        std::uint8_t jump[kStolenSize];
        jump[0] = 0xE9;
        const auto rel = static_cast<std::int32_t>(stub - (hook + kStolenSize));
        std::memcpy(jump + 1, &rel, sizeof(rel));
        if (!write_code(hook, jump, kStolenSize)) {
            VirtualFree(stub, 0, MEM_RELEASE);
            return ApplyResult::failed("failed to write hook jump");
        }

        char buf[112];
        std::snprintf(buf, sizeof(buf), "hook @ %p -> trampoline @ %p (write %p, reject %p)",
                      static_cast<void*>(hook), static_cast<void*>(stub),
                      static_cast<void*>(write_path), static_cast<void*>(miss_path));
        return ApplyResult::applied(buf);
    }
};

}  // namespace

std::unique_ptr<IPatch> make_mdl_studiohdr2_oob_patch() {
    return std::make_unique<MdlStudioHdr2OobPatch>();
}

}  // namespace csgopatch
