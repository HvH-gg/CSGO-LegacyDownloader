#pragma once

#include "patch/patch.h"

#include <memory>

namespace csgopatch {

/// Fix for the MDL `studiohdr2index` out-of-bounds write in
/// CMDLCache::ReadMDLFile (datacache.dll).
///
/// When a model is loaded, `studiohdr2index` is read straight from the .mdl file
/// and used as an offset: `*(pStudioHdr + studiohdr2index + 0x30) = handle`. The
/// only validation is that the index is non-zero, so a malicious server that
/// makes the client download a crafted model gets an out-of-bounds write ->
/// server -> client RCE.
///
/// This patch inserts a bounds check before that write: an index whose write
/// would fall outside the loaded model buffer is routed to the engine's existing
/// "model has no studiohdr2" rejection path, so the model is discarded instead of
/// corrupting memory. See the .cpp for the exact site and trampoline.
std::unique_ptr<IPatch> make_mdl_studiohdr2_oob_patch();

}  // namespace csgopatch
