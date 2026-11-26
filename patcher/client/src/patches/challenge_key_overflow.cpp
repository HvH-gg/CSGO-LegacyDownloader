#include "patches/challenge_key_overflow.h"

#include "scan/pattern.h"
#include "sys/memory.h"
#include "sys/module.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace csgopatch {
namespace {

// ---------------------------------------------------------------------------
//  Target
// ---------------------------------------------------------------------------
//  engine.dll, inside CBaseClientState::ProcessConnectionlessPacket: the two
//  S2C_CHALLENGE key reads (public key + signature), e.g. on 2018 (1.36.2.9) at
//  .text:1008C6AA and .text:1008C7AD:
//
//      8B CE                 mov  ecx, esi                 ; bf_read *this
//      50                    push eax                      ; nBits = keyLen * 8
//      8D 84 24 ?? 0A/06 00  lea  eax, [esp + dstBuffer]   ; char[1024] on stack
//      50                    push eax                      ; dst
//      E8 .. .. .. ..        call bf_read::ReadBits        ; rel32 rewritten below
//
//  bf_read::ReadBits is __thiscall: ecx = bf_read*, arg0 = dst, arg1 = nBits.
//  Neither key length is bounded before the read, so nBits can exceed the
//  1024-byte destination and smash the stack.
//
//  The lea displacement's low byte is the per-build stack layout of the two
//  char[1024] buffers (2018/2019/2020: 0x_34, two buffers 0x400 apart; 2016:
//  0x_D4, same 0x400 gap -> still char[1024]). Its next byte differs between the
//  two sites (0x0A vs 0x06) and is wildcarded. One candidate signature per build
//  layout is tried; the first that yields exactly two call sites to a single
//  ReadBits is used, so a single DLL covers every shipped build.
// ---------------------------------------------------------------------------

// Candidate call-site signatures, by per-build stack layout (see above). Each
// must match exactly kExpectedCallSites sites, all resolving to one ReadBits.
constexpr std::string_view kCallSiteSignatures[] = {
    "8B CE 50 8D 84 24 34 ?? 00 00 50 E8",  // 2018 / 2019 / 2020
    "8B CE 50 8D 84 24 D4 ?? 00 00 50 E8",  // 2016
};

// Offset of the 0xE8 CALL opcode within a signature match.
constexpr std::ptrdiff_t kCallOpcodeOffset = 11;

// Expected number of matching call sites in a correctly identified binary.
constexpr std::size_t kExpectedCallSites = 2;

// Maximum bits ReadBits may transfer into the 1024-byte key buffers.
// Declared as an enum so it is usable as an immediate in the inline-asm guard.
enum : std::uint32_t { kMaxChallengeBits = 1024 * 8 };

// Byte offset of bf_read::m_bOverflow within the bf_read object (verified against
// the `cmp byte ptr [esi+4], 0` overflow check emitted after the second call).
constexpr std::uint8_t kBfReadOverflowFlagOffset = 4;

// Resolved address of the original bf_read::ReadBits, read by the guard thunk.
void* g_original_read_bits = nullptr;

// ---------------------------------------------------------------------------
//  Guard thunk
// ---------------------------------------------------------------------------
//  Installed in place of bf_read::ReadBits at the two challenge call sites.
//  Preserves the exact __thiscall frame:
//      ecx      = bf_read*          [esp+4] = dst   [esp+8] = nBits
//  If nBits exceeds the destination, it marks the source buffer as overflowed
//  (so the engine aborts the handshake as an invalid packet) and forces nBits to
//  zero (so the forwarded read writes nothing), then tail-calls the real
//  ReadBits. In range, it forwards unchanged.
__declspec(naked) void read_bits_guard() {
    __asm {
        mov     eax, [esp + 8]                               // nBits
        cmp     eax, kMaxChallengeBits
        jbe     short forward
        mov     byte ptr [ecx + kBfReadOverflowFlagOffset], 1  // bf_read.m_bOverflow = true
        mov     dword ptr [esp + 8], 0                       // transfer nothing
    forward:
        mov     eax, g_original_read_bits
        jmp     eax
    }
}

// ---------------------------------------------------------------------------
//  Patch
// ---------------------------------------------------------------------------
class ChallengeKeyOverflowPatch final : public IPatch {
public:
    std::string_view id() const override { return "challenge-key-overflow"; }

    std::string_view description() const override {
        return "S2C challenge-reply key length unbounded -> client stack overflow "
               "(CBaseClientState::ProcessConnectionlessPacket)";
    }

    ApplyResult apply() override {
        const CodeSpan engine = module_image(L"engine.dll");
        if (engine.empty()) {
            return ApplyResult::not_applicable("engine.dll not loaded");
        }

        // Try each per-build candidate; the right one matches exactly the
        // expected number of call sites. A build we do not recognise matches
        // none and is reported NotApplicable, untouched.
        std::vector<std::uint8_t*> sites;
        for (const std::string_view sig : kCallSiteSignatures) {
            const auto pattern = Pattern::compile(sig);
            if (!pattern) {
                return ApplyResult::failed("internal: call-site signature failed to compile");
            }
            std::vector<std::uint8_t*> found = pattern->find_all(engine);
            if (found.size() == kExpectedCallSites) {
                sites = std::move(found);
                break;
            }
        }
        if (sites.size() != kExpectedCallSites) {
            return ApplyResult::not_applicable("no challenge-read signature matched this build");
        }

        // Verify every site is a rel32 CALL to the same target before touching
        // anything, and detect an already-patched image.
        void* original = nullptr;
        for (std::uint8_t* site : sites) {
            std::uint8_t* call = site + kCallOpcodeOffset;
            const auto target = decode_rel32_target(call, 0xE8);
            if (!target) {
                return ApplyResult::not_applicable("call site is not a rel32 CALL");
            }
            if (*target == static_cast<void*>(&read_bits_guard)) {
                return ApplyResult::already("call sites already redirected to guard");
            }
            if (original == nullptr) {
                original = *target;
            } else if (*target != original) {
                return ApplyResult::not_applicable("call sites resolve to different targets");
            }
        }

        // Commit: point the guard at the real ReadBits, then redirect both sites.
        g_original_read_bits = original;
        for (std::uint8_t* site : sites) {
            std::uint8_t* call = site + kCallOpcodeOffset;
            if (!redirect_call(call, reinterpret_cast<void*>(&read_bits_guard))) {
                return ApplyResult::failed("failed to rewrite call displacement");
            }
        }

        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "%zu call sites redirected; ReadBits @ %p, guard @ %p",
                      sites.size(), original, reinterpret_cast<void*>(&read_bits_guard));
        return ApplyResult::applied(buf);
    }
};

}  // namespace

std::unique_ptr<IPatch> make_challenge_key_overflow_patch() {
    return std::make_unique<ChallengeKeyOverflowPatch>();
}

}  // namespace csgopatch
