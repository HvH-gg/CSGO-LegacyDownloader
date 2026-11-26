#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace csgopatch {

/// Temporarily makes a code region writable+executable for the lifetime of the
/// object, restoring the original page protection on destruction. All code edits
/// go through this so protection is never left relaxed.
class ScopedUnprotect {
public:
    ScopedUnprotect(void* address, std::size_t size);
    ~ScopedUnprotect();

    ScopedUnprotect(const ScopedUnprotect&) = delete;
    ScopedUnprotect& operator=(const ScopedUnprotect&) = delete;

    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    void*         address_;
    std::size_t   size_;
    unsigned long original_protect_ = 0;
    bool          ok_ = false;
};

/// Overwrites `size` bytes at `dst`, flushing the instruction cache afterwards.
/// Returns false if the page protection could not be changed.
bool write_code(void* dst, const void* src, std::size_t size);

/// Decodes the absolute target of a 32-bit relative CALL/JMP whose opcode byte is
/// at `instruction` (expected 0xE8 for CALL / 0xE9 for JMP). Returns std::nullopt
/// if the opcode is not the expected one.
[[nodiscard]] std::optional<void*> decode_rel32_target(const std::uint8_t* instruction,
                                                       std::uint8_t expected_opcode);

/// Rewrites the rel32 displacement of the CALL at `call_site` (opcode 0xE8) so it
/// targets `new_target`, leaving the 0xE8 opcode byte intact. Returns the previous
/// absolute target on success (so the caller can chain to the original), or
/// std::nullopt if `call_site` is not a rel32 CALL or the write failed.
[[nodiscard]] std::optional<void*> redirect_call(std::uint8_t* call_site, void* new_target);

}  // namespace csgopatch
