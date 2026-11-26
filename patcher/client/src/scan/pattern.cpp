#include "scan/pattern.h"

#include <cctype>

namespace csgopatch {
namespace {

std::optional<int> hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return std::nullopt;
}

}  // namespace

std::optional<Pattern> Pattern::compile(std::string_view s) {
    Pattern p;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '?') {
            p.bytes_.push_back(0);
            p.mask_.push_back(false);
            i += (i + 1 < s.size() && s[i + 1] == '?') ? 2 : 1;
            continue;
        }
        if (i + 1 >= s.size()) return std::nullopt;  // dangling nibble
        const auto hi = hex_nibble(s[i]);
        const auto lo = hex_nibble(s[i + 1]);
        if (!hi || !lo) return std::nullopt;
        p.bytes_.push_back(static_cast<std::uint8_t>((*hi << 4) | *lo));
        p.mask_.push_back(true);
        i += 2;
    }
    if (p.bytes_.empty()) return std::nullopt;
    return p;
}

bool Pattern::matches_at(const std::uint8_t* p) const {
    for (std::size_t j = 0; j < bytes_.size(); ++j) {
        if (mask_[j] && p[j] != bytes_[j]) return false;
    }
    return true;
}

std::uint8_t* Pattern::find_first(const CodeSpan& span) const {
    if (span.empty() || span.size < bytes_.size()) return nullptr;
    const std::size_t last = span.size - bytes_.size();
    for (std::size_t i = 0; i <= last; ++i) {
        if (matches_at(span.data + i)) return span.data + i;
    }
    return nullptr;
}

std::vector<std::uint8_t*> Pattern::find_all(const CodeSpan& span) const {
    std::vector<std::uint8_t*> hits;
    if (span.empty() || span.size < bytes_.size()) return hits;
    const std::size_t last = span.size - bytes_.size();
    for (std::size_t i = 0; i <= last; ++i) {
        if (matches_at(span.data + i)) hits.push_back(span.data + i);
    }
    return hits;
}

}  // namespace csgopatch
