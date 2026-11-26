#pragma once

#include "patch/patch.h"

#include <memory>
#include <vector>

namespace csgopatch {

/// Summary of an apply_all run, for the caller and the log.
struct ApplySummary {
    int applied = 0;
    int already = 0;
    int not_applicable = 0;
    int failed = 0;

    [[nodiscard]] int total() const noexcept {
        return applied + already + not_applicable + failed;
    }
    /// True when nothing is left in a known-vulnerable state (no hard failures).
    [[nodiscard]] bool clean() const noexcept { return failed == 0; }
};

/// Owns the set of known patches and applies them in registration order.
class PatchRegistry {
public:
    /// Builds a registry populated with every patch known for this platform.
    static PatchRegistry with_builtin_patches();

    void add(std::unique_ptr<IPatch> patch);

    /// Applies every registered patch, logging one line per patch. Never throws.
    [[nodiscard]] ApplySummary apply_all();

private:
    std::vector<std::unique_ptr<IPatch>> patches_;
};

}  // namespace csgopatch
