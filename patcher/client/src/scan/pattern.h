#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace csgopatch {

/// A byte span over an executable image (or any memory region).
struct CodeSpan {
    std::uint8_t* data = nullptr;
    std::size_t   size = 0;

    [[nodiscard]] bool empty() const noexcept { return data == nullptr || size == 0; }
};

/// A compiled IDA-style byte signature, e.g. "8B CE 50 8D 84 24 34 ?? 00 00 50 E8".
///
/// Tokens are whitespace-separated. A two-hex-digit token matches that byte exactly;
/// "??" (or a single "?") matches any byte. Parsing happens once, up front, so a
/// pattern can be scanned repeatedly without re-parsing.
class Pattern {
public:
    /// Compiles a pattern string. Returns std::nullopt if the string is malformed
    /// (bad hex, or empty) so callers fail loudly rather than scan for garbage.
    static std::optional<Pattern> compile(std::string_view ida_style);

    [[nodiscard]] std::size_t length() const noexcept { return bytes_.size(); }

    /// Returns the first match inside `span`, or nullptr if there is none.
    [[nodiscard]] std::uint8_t* find_first(const CodeSpan& span) const;

    /// Returns every (non-overlapping-by-start) match inside `span`, in address order.
    [[nodiscard]] std::vector<std::uint8_t*> find_all(const CodeSpan& span) const;

private:
    Pattern() = default;
    [[nodiscard]] bool matches_at(const std::uint8_t* p) const;

    std::vector<std::uint8_t> bytes_;  ///< expected byte values (0 where wildcard)
    std::vector<bool>         mask_;    ///< true = compare, false = wildcard
};

}  // namespace csgopatch
