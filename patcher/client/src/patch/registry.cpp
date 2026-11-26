#include "patch/registry.h"

#include "patches/challenge_key_overflow.h"
#include "patches/splitscreen_slot_oob.h"
#include "sys/log.h"

namespace csgopatch {

PatchRegistry PatchRegistry::with_builtin_patches() {
    PatchRegistry registry;
    // Server -> client remote code execution fixes. Add new patches here; the
    // apply loop and reporting pick them up automatically.
    registry.add(make_challenge_key_overflow_patch());
    registry.add(make_splitscreen_slot_oob_patch());
    return registry;
}

void PatchRegistry::add(std::unique_ptr<IPatch> patch) {
    if (patch) patches_.push_back(std::move(patch));
}

ApplySummary PatchRegistry::apply_all() {
    ApplySummary summary;
    for (const auto& patch : patches_) {
        ApplyResult result = patch->apply();
        const char* id = patch->id().data();
        const char* detail = result.detail.c_str();
        switch (result.status) {
            case ApplyStatus::Applied:
                ++summary.applied;
                Log::info("patch '%s' applied: %s", id, detail);
                break;
            case ApplyStatus::AlreadyApplied:
                ++summary.already;
                Log::info("patch '%s' already applied: %s", id, detail);
                break;
            case ApplyStatus::NotApplicable:
                ++summary.not_applicable;
                Log::warn("patch '%s' not applicable: %s", id, detail);
                break;
            case ApplyStatus::Failed:
                ++summary.failed;
                Log::error("patch '%s' FAILED: %s", id, detail);
                break;
        }
    }
    Log::info("apply_all done: %d applied, %d already, %d n/a, %d failed",
              summary.applied, summary.already, summary.not_applicable, summary.failed);
    return summary;
}

}  // namespace csgopatch
