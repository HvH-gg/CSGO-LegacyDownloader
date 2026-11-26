#pragma once

#include "patch/patch.h"

#include <memory>

namespace csgopatch {

/// Fix for the S2C connectionless challenge-reply stack buffer overflow in
/// CBaseClientState::ProcessConnectionlessPacket (engine.dll).
///
/// The handler reads an attacker-controlled public-key length and signature
/// length from the challenge packet and feeds each straight to
/// bf_read::ReadBits(dst, len * 8) targeting a fixed char[1024] stack buffer,
/// with no bound on the length. A malicious server can therefore overflow the
/// client's stack during the connect handshake. See the .cpp for the exact
/// call sites, signature and guard.
std::unique_ptr<IPatch> make_challenge_key_overflow_patch();

}  // namespace csgopatch
