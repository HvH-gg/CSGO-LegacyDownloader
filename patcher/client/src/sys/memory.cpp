#include "sys/memory.h"

#include <windows.h>

#include <cstring>

namespace csgopatch {
namespace {

constexpr std::uint8_t kCallRel32 = 0xE8;
constexpr std::size_t  kRel32InsnLen = 5;  // opcode + int32 displacement

}  // namespace

ScopedUnprotect::ScopedUnprotect(void* address, std::size_t size)
    : address_(address), size_(size) {
    DWORD previous = 0;
    if (VirtualProtect(address_, size_, PAGE_EXECUTE_READWRITE, &previous)) {
        original_protect_ = previous;
        ok_ = true;
    }
}

ScopedUnprotect::~ScopedUnprotect() {
    if (ok_) {
        DWORD ignored = 0;
        VirtualProtect(address_, size_, original_protect_, &ignored);
    }
}

bool write_code(void* dst, const void* src, std::size_t size) {
    ScopedUnprotect guard(dst, size);
    if (!guard.ok()) return false;
    std::memcpy(dst, src, size);
    FlushInstructionCache(GetCurrentProcess(), dst, size);
    return true;
}

std::optional<void*> decode_rel32_target(const std::uint8_t* instruction,
                                         std::uint8_t expected_opcode) {
    if (instruction == nullptr || *instruction != expected_opcode) return std::nullopt;
    std::int32_t rel = 0;
    std::memcpy(&rel, instruction + 1, sizeof(rel));
    auto* next = const_cast<std::uint8_t*>(instruction) + kRel32InsnLen;
    return static_cast<void*>(next + rel);
}

std::optional<void*> redirect_call(std::uint8_t* call_site, void* new_target) {
    const auto previous = decode_rel32_target(call_site, kCallRel32);
    if (!previous) return std::nullopt;

    const auto displacement =
        static_cast<std::int32_t>(reinterpret_cast<std::uint8_t*>(new_target) -
                                  (call_site + kRel32InsnLen));
    if (!write_code(call_site + 1, &displacement, sizeof(displacement))) return std::nullopt;

    return previous;
}

}  // namespace csgopatch
