#pragma once

#include "patch/patch.h"

#include <memory>

namespace csgopatch {

/// Fix for the SplitScreen slot out-of-bounds remote code execution.
///
/// The CSVCMsg_SplitScreen message carries a `slot` field that a malicious
/// server controls. The engine uses it as an index into the array of
/// split-screen player objects with no range check, then dereferences a vtable
/// and makes an indirect virtual call — a reliable server -> client RCE.
///
/// The message's own `type` field is validated to {0,1} by the generated
/// parser, but `slot` is not. This patch adds that missing validation at the
/// message parser (ReadFromBuffer): a slot outside the valid range makes the
/// parse fail, so the engine discards the packet before the vulnerable handler
/// runs. See the .cpp for the exact chokepoint and bound.
std::unique_ptr<IPatch> make_splitscreen_slot_oob_patch();

}  // namespace csgopatch
