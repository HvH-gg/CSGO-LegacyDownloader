#pragma once

#include "scan/pattern.h"

#include <string>

namespace csgopatch {

/// Returns the in-memory image span of a loaded module (e.g. L"engine.dll"),
/// or an empty span if the module is not currently loaded.
[[nodiscard]] CodeSpan module_image(const wchar_t* module_name);

/// Blocks up to `timeout_ms` for a module to appear, polling every `poll_ms`.
/// Returns true once it is loaded. Used because the launcher injects the patcher
/// before the engine has mapped its DLLs.
[[nodiscard]] bool wait_for_module(const wchar_t* module_name,
                                   unsigned timeout_ms,
                                   unsigned poll_ms = 25);

}  // namespace csgopatch
