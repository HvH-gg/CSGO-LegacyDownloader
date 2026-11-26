#include "patch/registry.h"
#include "sys/log.h"
#include "sys/module.h"

#include <windows.h>

namespace {

// Engine DLL that hosts every patch target. The launcher injects this patcher
// before the game maps it, so we wait for it rather than assume it is present.
constexpr wchar_t kEngineModule[] = L"engine.dll";
constexpr unsigned kEngineWaitMs = 30'000;

DWORD WINAPI patcher_thread(LPVOID) {
    using namespace csgopatch;

    Log::info("patcher thread started (pid %lu)", GetCurrentProcessId());

    if (!wait_for_module(kEngineModule, kEngineWaitMs)) {
        Log::error("engine.dll did not load within %u ms; no patches applied", kEngineWaitMs);
        return 0;
    }

    ApplySummary summary = PatchRegistry::with_builtin_patches().apply_all();
    if (!summary.clean()) {
        Log::error("%d patch(es) failed to apply; the client may still be vulnerable",
                   summary.failed);
    }
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Do no real work under the loader lock: hand off to a worker thread.
        if (HANDLE t = CreateThread(nullptr, 0, patcher_thread, nullptr, 0, nullptr)) {
            CloseHandle(t);
        }
    }
    return TRUE;
}
