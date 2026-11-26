// csgo-patched-launcher: starts the legacy CS:GO client with the security
// patcher injected before any game code runs.
//
// It lives in the game folder next to csgo.exe and csgo-patcher.dll:
//   1. creates csgo.exe suspended (so no engine code has executed yet),
//   2. injects csgo-patcher.dll via LoadLibraryW in the target,
//   3. resumes the game.
//
// Any command-line arguments passed to the launcher are forwarded to csgo.exe,
// on top of the mandatory "-steam -insecure" that a retired build must run with.

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace {

constexpr wchar_t kGameExe[]    = L"csgo.exe";
constexpr wchar_t kPatcherDll[] = L"csgo-patcher.dll";
constexpr wchar_t kRequiredArgs[] = L"-steam -insecure";

void fail(const wchar_t* what) {
    std::wstring msg = what;
    msg += L"\n\nError code: " + std::to_wstring(GetLastError());
    MessageBoxW(nullptr, msg.c_str(), L"CS:GO Patched Launcher", MB_ICONERROR | MB_OK);
}

/// Directory containing this launcher, with a trailing backslash.
std::wstring launcher_dir() {
    std::wstring path(MAX_PATH, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(n);
    const std::size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? L".\\" : path.substr(0, slash + 1);
}

/// Forwarded launcher arguments (everything after argv[0]), space-joined.
std::wstring forwarded_args() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring joined;
    for (int i = 1; i < argc; ++i) {
        if (!joined.empty()) joined += L' ';
        joined += argv[i];
    }
    LocalFree(argv);
    return joined;
}

/// Injects `dll_path` into `process` by running LoadLibraryW there. Returns true
/// once the remote load thread has completed.
bool inject(HANDLE process, const std::wstring& dll_path) {
    const SIZE_T bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;

    bool ok = WriteProcessMemory(process, remote, dll_path.c_str(), bytes, nullptr) != 0;
    if (ok) {
        auto load_library =
            reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE thread = load_library
            ? CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr)
            : nullptr;
        if (thread) {
            WaitForSingleObject(thread, INFINITE);
            DWORD remote_module = 0;
            GetExitCodeThread(thread, &remote_module);
            ok = remote_module != 0;  // LoadLibraryW returned non-NULL in the target
            CloseHandle(thread);
        } else {
            ok = false;
        }
    }

    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return ok;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    const std::wstring dir = launcher_dir();
    const std::wstring game = dir + kGameExe;
    const std::wstring dll  = dir + kPatcherDll;

    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        fail(L"csgo-patcher.dll was not found next to the launcher.");
        return 1;
    }

    std::wstring command_line = L"\"" + game + L"\" " + kRequiredArgs;
    if (const std::wstring extra = forwarded_args(); !extra.empty()) {
        command_line += L' ';
        command_line += extra;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    std::vector<wchar_t> mutable_cmd(command_line.begin(), command_line.end());
    mutable_cmd.push_back(L'\0');

    if (!CreateProcessW(game.c_str(), mutable_cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, dir.c_str(), &startup, &process)) {
        fail(L"Could not start csgo.exe.");
        return 1;
    }

    if (!inject(process.hProcess, dll)) {
        fail(L"Could not inject the security patcher. The game was not started.");
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return 1;
    }

    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
