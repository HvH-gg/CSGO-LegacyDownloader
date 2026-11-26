#include "sys/module.h"

#include <windows.h>
#include <psapi.h>

namespace csgopatch {

CodeSpan module_image(const wchar_t* module_name) {
    const HMODULE module = GetModuleHandleW(module_name);
    if (module == nullptr) return {};

    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) return {};

    return CodeSpan{reinterpret_cast<std::uint8_t*>(info.lpBaseOfDll), info.SizeOfImage};
}

bool wait_for_module(const wchar_t* module_name, unsigned timeout_ms, unsigned poll_ms) {
    const unsigned steps = poll_ms ? (timeout_ms / poll_ms) + 1 : 1;
    for (unsigned i = 0; i < steps; ++i) {
        if (GetModuleHandleW(module_name) != nullptr) return true;
        Sleep(poll_ms);
    }
    return GetModuleHandleW(module_name) != nullptr;
}

}  // namespace csgopatch
