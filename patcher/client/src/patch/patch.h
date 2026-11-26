#pragma once

#include <string>
#include <string_view>

namespace csgopatch {

/// Outcome of attempting to apply a single patch.
enum class ApplyStatus {
    Applied,         ///< the patch was located and written this call
    AlreadyApplied,  ///< the target was already patched (idempotent re-run)
    NotApplicable,   ///< the signature did not match this binary/build; left untouched
    Failed,          ///< the signature matched but applying it failed (see detail)
};

struct ApplyResult {
    ApplyStatus status;
    std::string detail;  ///< human-readable context, always safe to log

    static ApplyResult applied(std::string d = {})        { return {ApplyStatus::Applied, std::move(d)}; }
    static ApplyResult already(std::string d = {})         { return {ApplyStatus::AlreadyApplied, std::move(d)}; }
    static ApplyResult not_applicable(std::string d = {})  { return {ApplyStatus::NotApplicable, std::move(d)}; }
    static ApplyResult failed(std::string d = {})          { return {ApplyStatus::Failed, std::move(d)}; }
};

/// A single security fix applied to the running game.
///
/// Every patch must be self-validating: it locates its target by signature and
/// verifies the target before writing. If the binary does not match (wrong build,
/// already-patched, unexpected bytes) it must report NotApplicable/AlreadyApplied
/// and change nothing — a patch is never forced onto bytes it did not recognise.
class IPatch {
public:
    virtual ~IPatch() = default;

    /// Stable machine identifier, e.g. "challenge-key-overflow".
    [[nodiscard]] virtual std::string_view id() const = 0;

    /// One-line human description of the vulnerability being closed.
    [[nodiscard]] virtual std::string_view description() const = 0;

    /// Locates and applies the fix. Must be safe to call more than once.
    [[nodiscard]] virtual ApplyResult apply() = 0;
};

}  // namespace csgopatch
