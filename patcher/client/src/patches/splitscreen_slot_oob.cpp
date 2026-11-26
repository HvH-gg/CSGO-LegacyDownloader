#include "patches/splitscreen_slot_oob.h"

#include "scan/pattern.h"
#include "sys/memory.h"
#include "sys/module.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace csgopatch {
namespace {

// ---------------------------------------------------------------------------
//  Target
// ---------------------------------------------------------------------------
//  engine.dll. The generated parser for CSVCMsg_SplitScreen (the message's
//  ReadFromBuffer / MergePartialFromBuffer) decodes three varint fields:
//      field 1 type         -> object + 8    (validated to {0,1} already)
//      field 2 slot         -> object + 12   (NOT range-checked  <-- the bug)
//      field 3 player_index -> object + 16
//  with the present-field bitmask at object + 24. These field offsets and the
//  has-bits layout are identical across every shipped build (2016 / 2018 / 2019
//  / 2020), so the guard below is build-independent; only the parser prologue
//  changes shape between compilers and needs a per-build signature.
//
//  The handler later uses `slot` as an unchecked index into the split-screen
//  player array, then makes an indirect virtual call through the fetched
//  pointer. We close this at the parser: the function pointer in the message
//  vtable is replaced with a guard that runs the original parser and then
//  rejects a parse whose decoded `slot` is out of range, so the engine treats
//  the packet as malformed and never dispatches it.
// ---------------------------------------------------------------------------

// Candidate parser prologues, one per build-compiler shape. Each must be unique
// (exactly one match) in its own engine.dll. The first that matches is used, so a
// single DLL covers every shipped build. 2018/2019/2020 share one prologue; 2016
// aligns the stack (and 83 EC 10), so its prologue differs and runs longer to
// stay unique (its two jae rel8 displacements are wildcarded).
constexpr std::string_view kParserSignatures[] = {
    // 2018 / 2019 / 2020
    "55 8B EC 83 EC 08 56 8B 75 08 57 8B F9 0F 1F 00 8B 4E 04 3B 4E 08 73",
    // 2016
    "55 8B EC 83 E4 F8 83 EC 10 56 8B 75 08 57 8B F9 8B 4E 04 3B 4E 08 73 ?? "
    "8A 01 3C 80 73 ?? 0F B6 C0 89 46 14 8D 41 01 8B 4E 14",
};

// Message object field offsets (see above).
constexpr std::size_t   kHasBitsOffset = 24;
constexpr std::uint32_t kSlotPresentBit = 2;  // field 2 present
constexpr std::size_t   kSlotOffset    = 12;

// Valid split-screen slots are [0, MAX_SPLITSCREEN_CLIENTS). CS:GO supports two
// split-screen slots; the array indexed by `slot` is sized accordingly. A real
// client never sends a larger slot, so bounding here cannot reject legitimate
// traffic. (Confirm against the handler's array size when extending to other
// builds.)
constexpr std::uint32_t kMaxSplitScreen = 2;

using ReadFromBufferFn = int(__thiscall*)(void* self, void* buffer);
ReadFromBufferFn g_original_parser = nullptr;

// Emulated __thiscall (self in ECX) via __fastcall; EDX is unused by the callee.
int __fastcall read_from_buffer_guard(void* self, void* /*edx*/, void* buffer) {
    const int parsed_ok = g_original_parser(self, buffer);
    if (!parsed_ok) return parsed_ok;

    const auto* object = reinterpret_cast<const std::uint8_t*>(self);
    const std::uint32_t has_bits =
        *reinterpret_cast<const std::uint32_t*>(object + kHasBitsOffset);
    if (has_bits & kSlotPresentBit) {
        const std::int32_t slot =
            *reinterpret_cast<const std::int32_t*>(object + kSlotOffset);
        if (static_cast<std::uint32_t>(slot) >= kMaxSplitScreen) {
            return 0;  // malformed -> engine drops the packet before dispatch
        }
    }
    return parsed_ok;
}

// Finds every 4-byte little-endian occurrence of `value` inside the module image
// (the message vtable slots that hold the parser pointer).
std::vector<std::uint8_t*> find_pointer_slots(const CodeSpan& image, std::uint8_t* value) {
    std::vector<std::uint8_t*> slots;
    if (image.size < sizeof(void*)) return slots;
    const auto needle = reinterpret_cast<std::uintptr_t>(value);
    for (std::size_t i = 0; i + sizeof(void*) <= image.size; ++i) {
        if (*reinterpret_cast<std::uintptr_t*>(image.data + i) == needle) {
            slots.push_back(image.data + i);
        }
    }
    return slots;
}

class SplitScreenSlotOobPatch final : public IPatch {
public:
    std::string_view id() const override { return "splitscreen-slot-oob"; }

    std::string_view description() const override {
        return "CSVCMsg_SplitScreen slot used as unchecked array index -> server "
               "-> client RCE; slot range-checked at the message parser";
    }

    ApplyResult apply() override {
        const CodeSpan engine = module_image(L"engine.dll");
        if (engine.empty()) return ApplyResult::not_applicable("engine.dll not loaded");

        // Try each per-build prologue; the matching build yields exactly one.
        std::vector<std::uint8_t*> parser;
        for (const std::string_view sig : kParserSignatures) {
            const auto pattern = Pattern::compile(sig);
            if (!pattern) return ApplyResult::failed("internal: parser signature failed to compile");
            std::vector<std::uint8_t*> found = pattern->find_all(engine);
            if (found.size() == 1) {
                parser = std::move(found);
                break;
            }
        }
        if (parser.size() != 1) {
            return ApplyResult::not_applicable("no split-screen parser signature matched this build");
        }

        std::uint8_t* parser_fn = parser.front();
        auto* guard = reinterpret_cast<std::uint8_t*>(&read_from_buffer_guard);

        // The parser pointer lives in the message vtable(s). Replace each slot.
        const std::vector<std::uint8_t*> slots = find_pointer_slots(engine, parser_fn);
        if (slots.empty()) {
            // Already redirected to the guard?
            if (!find_pointer_slots(engine, guard).empty()) {
                return ApplyResult::already("parser already redirected to guard");
            }
            return ApplyResult::not_applicable("parser pointer not found in any vtable");
        }

        g_original_parser = reinterpret_cast<ReadFromBufferFn>(parser_fn);
        int patched = 0;
        for (std::uint8_t* slot : slots) {
            if (!write_code(slot, &guard, sizeof(guard))) {
                return ApplyResult::failed("failed to rewrite message vtable slot");
            }
            ++patched;
        }

        char buf[112];
        std::snprintf(buf, sizeof(buf), "%d vtable slot(s) redirected; parser @ %p, guard @ %p",
                      patched, static_cast<void*>(parser_fn), static_cast<void*>(guard));
        return ApplyResult::applied(buf);
    }
};

}  // namespace

std::unique_ptr<IPatch> make_splitscreen_slot_oob_patch() {
    return std::make_unique<SplitScreenSlotOobPatch>();
}

}  // namespace csgopatch
