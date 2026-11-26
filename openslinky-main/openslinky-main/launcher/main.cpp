// SlinkyClient launcher / injector.
//
// The client is a JVMTI/JNI DLL that must live inside the Minecraft JVM. This
// executable locates that JVM, waits until it is ready, and injects
// libslinky.dll with a remote LoadLibraryW call. libslinky.dll's DllMain starts
// the client asynchronously, so no further action is required.
//
// Build: see launcher/CMakeLists.txt. Output: SlinkyClient.exe + libslinky.dll.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#include <io.h>

namespace {

constexpr DWORD kProcessQueryLimitedInformation = 0x1000;
constexpr DWORD kMaxPath = 32768;

template <typename Fn>
Fn load_export(HMODULE module, const char* name) {
    FARPROC proc = GetProcAddress(module, name);
    Fn fn{};
    static_assert(sizeof(fn) == sizeof(proc), "function pointer size mismatch");
    std::memcpy(&fn, &proc, sizeof(fn));
    return fn;
}

struct Options {
    DWORD pid = 0;
    std::wstring process_name = L"javaw.exe";
    std::wstring dll_path;
    std::wstring launch_command;
    double wait_seconds = 60.0;
    bool require_window = true;
    bool list_only = false;
    bool no_pause = false;
};

struct Candidate {
    DWORD pid = 0;
    std::wstring exe_name;
    std::wstring exe_path;
    std::wstring command_line;
    bool has_jvm = false;
    bool has_window = false;
    int score = 0;
};

void out(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfwprintf(stdout, format, args);
    va_end(args);
    std::fflush(stdout);
}

void pause_if_console() {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &mode)) {
        std::fputws(L"\nPress Enter to close...", stdout);
        std::fflush(stdout);
        wchar_t buffer[8];
        std::fgetws(buffer, 8, stdin);
    }
}

bool has_console_output() {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    return output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode) != FALSE;
}

void print_banner() {
    out(L"\n");
    out(L"  +----------------------------------------------+\n");
    out(L"  |        S L I N K Y   C L I E N T             |\n");
    out(L"  |              I N J E C T O R                 |\n");
    out(L"  +----------------------------------------------+\n");
    out(L"\n");
}

// Shows a live 5..1 countdown, then lets the window close on its own.
void countdown_and_close(unsigned seconds) {
    for (unsigned remaining = seconds; remaining > 0u; --remaining) {
        out(L"\r  [*] Closing in %u second%ls...   ", remaining,
            remaining == 1u ? L"" : L"s");
        Sleep(1000u);
    }
    out(L"\r  [+] Done. Good game.                            \n");
}

std::wstring exe_directory() {
    wchar_t path[kMaxPath]{};
    DWORD len = GetModuleFileNameW(nullptr, path, kMaxPath);
    if (len == 0 || len >= kMaxPath) {
        return L".";
    }
    std::wstring full(path, len);
    const size_t slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : full.substr(0, slash);
}

std::wstring absolute_path(const std::wstring& path) {
    wchar_t buffer[kMaxPath]{};
    DWORD len = GetFullPathNameW(path.c_str(), kMaxPath, buffer, nullptr);
    if (len == 0 || len >= kMaxPath) {
        return path;
    }
    return std::wstring(buffer, len);
}

std::wstring file_name_of(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool file_exists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring lower(std::wstring value) {
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

std::wstring query_process_command_line(HANDLE process) {
    using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static auto nt_query = load_export<NtQueryInformationProcessFn>(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    if (nt_query == nullptr) {
        return {};
    }

    constexpr ULONG kProcessCommandLineInformation = 60;
    ULONG needed = 0;
    nt_query(process, kProcessCommandLineInformation, nullptr, 0, &needed);
    if (needed == 0) {
        return {};
    }

    std::vector<BYTE> buffer(needed + sizeof(wchar_t) * 2);
    ULONG written = 0;
    if (nt_query(process, kProcessCommandLineInformation, buffer.data(),
                 static_cast<ULONG>(buffer.size()), &written) != 0) {
        return {};
    }

    auto* unicode = reinterpret_cast<UNICODE_STRING*>(buffer.data());
    if (unicode->Buffer == nullptr || unicode->Length == 0) {
        return {};
    }
    return std::wstring(unicode->Buffer, unicode->Length / sizeof(wchar_t));
}

std::wstring query_process_image_path(HANDLE process) {
    wchar_t path[kMaxPath]{};
    DWORD len = kMaxPath;
    if (!QueryFullProcessImageNameW(process, 0, path, &len)) {
        return {};
    }
    return std::wstring(path, len);
}

bool process_has_module(DWORD pid, const wchar_t* module_name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, module_name) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

struct WindowSearch {
    DWORD pid = 0;
    HWND window = nullptr;
    std::wstring title;
};

BOOL CALLBACK find_window_cb(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->pid || !IsWindowVisible(hwnd)) {
        return TRUE;
    }
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return TRUE;
    }
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, title.data(), length + 1);
    title.resize(static_cast<size_t>(length));
    search->window = hwnd;
    search->title = title;
    return FALSE;
}

bool find_process_window(DWORD pid, std::wstring* out_title) {
    WindowSearch search{};
    search.pid = pid;
    EnumWindows(&find_window_cb, reinterpret_cast<LPARAM>(&search));
    if (search.window == nullptr) {
        return false;
    }
    if (out_title != nullptr) {
        *out_title = search.title;
    }
    return true;
}

int score_candidate(const Candidate& candidate) {
    int score = 0;
    if (candidate.has_jvm) {
        score += 100;
    }
    if (candidate.has_window) {
        score += 25;
    }
    const std::wstring command = lower(candidate.command_line);
    if (command.find(L"minecraft") != std::wstring::npos) {
        score += 50;
    }
    if (command.find(L"forge") != std::wstring::npos) {
        score += 10;
    }
    if (command.find(L"lunar") != std::wstring::npos) {
        score += 10;
    }
    if (_wcsicmp(candidate.exe_name.c_str(), L"javaw.exe") == 0) {
        score += 5;
    }
    return score;
}

bool collect_candidate(DWORD pid, const Options& options, Candidate* candidate) {
    HANDLE process = OpenProcess(kProcessQueryLimitedInformation | PROCESS_VM_READ, FALSE, pid);
    if (process == nullptr) {
        return false;
    }

    candidate->pid = pid;
    candidate->exe_path = query_process_image_path(process);
    candidate->exe_name = file_name_of(candidate->exe_path);
    candidate->command_line = query_process_command_line(process);
    CloseHandle(process);

    if (candidate->exe_name.empty()) {
        return false;
    }
    if (!options.process_name.empty() &&
        _wcsicmp(candidate->exe_name.c_str(), options.process_name.c_str()) != 0) {
        return false;
    }

    candidate->has_jvm = process_has_module(pid, L"jvm.dll");
    std::wstring window_title;
    candidate->has_window = find_process_window(pid, &window_title);
    candidate->score = score_candidate(*candidate);
    return true;
}

std::vector<Candidate> enumerate_candidates(const Options& options) {
    std::vector<Candidate> candidates;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return candidates;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == 0 || entry.th32ProcessID == GetCurrentProcessId()) {
                continue;
            }
            if (!options.process_name.empty() &&
                _wcsicmp(entry.szExeFile, options.process_name.c_str()) != 0) {
                continue;
            }
            Candidate candidate{};
            if (collect_candidate(entry.th32ProcessID, options, &candidate)) {
                candidates.push_back(std::move(candidate));
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return candidates;
}

bool is_target_ready(const Candidate& candidate, const Options& options) {
    if (!candidate.has_jvm) {
        return false;
    }
    if (options.require_window && !candidate.has_window) {
        return false;
    }
    return true;
}

bool select_best_candidate(const Options& options, Candidate* out_candidate) {
    std::vector<Candidate> candidates = enumerate_candidates(options);
    if (candidates.empty()) {
        return false;
    }
    Candidate* best = nullptr;
    for (Candidate& candidate : candidates) {
        if (!is_target_ready(candidate, options)) {
            continue;
        }
        if (best == nullptr || candidate.score > best->score) {
            best = &candidate;
        }
    }
    if (best == nullptr) {
        return false;
    }
    *out_candidate = *best;
    return true;
}

bool is_64_bit_process(DWORD pid) {
    HANDLE process = OpenProcess(kProcessQueryLimitedInformation, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    BOOL wow64 = FALSE;
    const bool ok = IsWow64Process(process, &wow64) != FALSE;
    CloseHandle(process);
    return ok && !wow64;
}

bool validate_dll_architecture(const std::wstring& dll_path, std::wstring* error) {
    HANDLE file = CreateFileW(dll_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *error = L"Cannot open the client DLL: " + dll_path;
        return false;
    }

    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    bool ok = ReadFile(file, &dos, sizeof(dos), &read, nullptr) && read == sizeof(dos) &&
              dos.e_magic == IMAGE_DOS_SIGNATURE;
    IMAGE_NT_HEADERS64 nt{};
    if (ok) {
        SetFilePointer(file, dos.e_lfanew, nullptr, FILE_BEGIN);
        ok = ReadFile(file, &nt, sizeof(nt), &read, nullptr) && read == sizeof(nt) &&
             nt.Signature == IMAGE_NT_SIGNATURE;
    }
    CloseHandle(file);

    if (!ok) {
        *error = L"The client DLL is not a valid PE image: " + dll_path;
        return false;
    }
    if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        *error = L"The client DLL is not 64-bit x86-64.";
        return false;
    }
    return true;
}

bool inject_dll(DWORD pid, const std::wstring& dll_path, DWORD* error_code) {
    const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                         PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    HANDLE process = OpenProcess(access, FALSE, pid);
    if (process == nullptr) {
        *error_code = GetLastError();
        return false;
    }

    const SIZE_T byte_count = (dll_path.size() + 1) * sizeof(wchar_t);
    void* remote_buffer = VirtualAllocEx(process, nullptr, byte_count,
                                         MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_buffer == nullptr) {
        *error_code = GetLastError();
        CloseHandle(process);
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_buffer, dll_path.c_str(), byte_count, &written) ||
        written != byte_count) {
        *error_code = GetLastError();
        VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    auto load_library = load_export<LPTHREAD_START_ROUTINE>(
        GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (load_library == nullptr) {
        *error_code = GetLastError();
        VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library,
                                       remote_buffer, 0, nullptr);
    if (thread == nullptr) {
        *error_code = GetLastError();
        VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    const DWORD wait = WaitForSingleObject(thread, 60000);
    if (wait != WAIT_OBJECT_0) {
        *error_code = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        CloseHandle(thread);
        VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    DWORD module_handle = 0;
    GetExitCodeThread(thread, &module_handle);
    CloseHandle(thread);
    VirtualFreeEx(process, remote_buffer, 0, MEM_RELEASE);
    CloseHandle(process);

    if (module_handle == 0) {
        *error_code = ERROR_DLL_INIT_FAILED;
        return false;
    }
    *error_code = 0;
    return true;
}

void print_usage() {
    out(L"SlinkyClient - Minecraft client launcher\n");
    out(L"\n");
    out(L"Usage:\n");
    out(L"  SlinkyClient.exe [options]\n");
    out(L"\n");
    out(L"Options:\n");
    out(L"  --pid <id>           Inject into this process id.\n");
    out(L"  --process <name>     Target executable name (default: javaw.exe).\n");
    out(L"  --dll <path>         Client DLL (default: libslinky.dll next to this exe).\n");
    out(L"  --wait <seconds>     Wait for a ready Minecraft JVM (default: 60, 0 = no wait).\n");
    out(L"  --no-window          Do not require a visible game window.\n");
    out(L"  --launch <command>   Start a command line first, then inject.\n");
    out(L"  --list               List candidate Java processes and exit.\n");
    out(L"  --no-pause           Do not pause before exiting.\n");
    out(L"  --help               Show this help.\n");
}

bool parse_options(int argc, wchar_t** argv, Options* options, bool* show_help) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto next_value = [&](const wchar_t* name) -> const wchar_t* {
            if (i + 1 >= argc) {
                out(L"Missing value for %ls\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            *show_help = true;
        } else if (arg == L"--list") {
            options->list_only = true;
        } else if (arg == L"--no-window") {
            options->require_window = false;
        } else if (arg == L"--no-pause") {
            options->no_pause = true;
        } else if (arg == L"--pid") {
            const wchar_t* value = next_value(L"--pid");
            if (value == nullptr) return false;
            options->pid = static_cast<DWORD>(std::wcstoul(value, nullptr, 10));
        } else if (arg == L"--process") {
            const wchar_t* value = next_value(L"--process");
            if (value == nullptr) return false;
            options->process_name = value;
        } else if (arg == L"--dll") {
            const wchar_t* value = next_value(L"--dll");
            if (value == nullptr) return false;
            options->dll_path = value;
        } else if (arg == L"--wait") {
            const wchar_t* value = next_value(L"--wait");
            if (value == nullptr) return false;
            options->wait_seconds = std::wcstod(value, nullptr);
        } else if (arg == L"--launch") {
            const wchar_t* value = next_value(L"--launch");
            if (value == nullptr) return false;
            options->launch_command = value;
        } else {
            out(L"Unknown option: %ls\n", arg.c_str());
            return false;
        }
    }
    return true;
}

bool launch_command(const std::wstring& command, DWORD* out_pid) {
    std::wstring mutable_command = command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring working_directory = exe_directory();
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP, nullptr, working_directory.c_str(),
                        &startup, &process)) {
        out(L"CreateProcess failed: %lu\n", GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    *out_pid = process.dwProcessId;
    return true;
}

void list_candidates(const Options& options) {
    std::vector<Candidate> candidates = enumerate_candidates(options);
    if (candidates.empty()) {
        out(L"No matching Java processes found.\n");
        return;
    }
    out(L"%-8ls %-16ls %-8ls %-8ls %ls\n", L"PID", L"EXE", L"JVM", L"WINDOW", L"COMMAND");
    for (const Candidate& candidate : candidates) {
        out(L"%-8lu %-16ls %-8ls %-8ls %ls\n",
            static_cast<unsigned long>(candidate.pid),
            candidate.exe_name.c_str(),
            candidate.has_jvm ? L"yes" : L"no",
            candidate.has_window ? L"yes" : L"no",
            candidate.command_line.c_str());
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleTitleW(L"Slinky Injector");

    Options options{};
    bool show_help = false;
    if (!parse_options(argc, argv, &options, &show_help)) {
        print_usage();
        if (!options.no_pause) pause_if_console();
        return 2;
    }
    if (show_help) {
        print_usage();
        return 0;
    }

    const bool interactive = has_console_output();
    if (interactive) {
        print_banner();
    }

    if (options.list_only) {
        list_candidates(options);
        if (!options.no_pause) pause_if_console();
        return 0;
    }

    if (options.dll_path.empty()) {
        options.dll_path = exe_directory() + L"\\libslinky.dll";
    }
    options.dll_path = absolute_path(options.dll_path);

    std::wstring dll_error;
    if (!file_exists(options.dll_path)) {
        out(L"Client DLL not found: %ls\n", options.dll_path.c_str());
        if (!options.no_pause) pause_if_console();
        return 3;
    }
    if (!validate_dll_architecture(options.dll_path, &dll_error)) {
        out(L"%ls\n", dll_error.c_str());
        if (!options.no_pause) pause_if_console();
        return 3;
    }

    DWORD target_pid = options.pid;
    if (!options.launch_command.empty()) {
        out(L"  [*] Starting game launcher...\n");
        if (!launch_command(options.launch_command, &target_pid)) {
            if (!options.no_pause) pause_if_console();
            return 4;
        }
        options.pid = target_pid;
    }

    Candidate target{};
    const DWORD deadline = GetTickCount() + static_cast<DWORD>(options.wait_seconds * 1000.0);

    if (options.pid != 0) {
        out(L"  [*] Waiting for process %lu...\n", static_cast<unsigned long>(options.pid));
        while (true) {
            Candidate candidate{};
            if (collect_candidate(options.pid, options, &candidate) &&
                is_target_ready(candidate, options)) {
                target = candidate;
                break;
            }
            if (GetTickCount() >= deadline) {
                out(L"  [!] Process %lu did not become ready (JVM%ls).\n",
                    static_cast<unsigned long>(options.pid),
                    options.require_window ? L" + window" : L"");
                if (!options.no_pause) pause_if_console();
                return 5;
            }
            Sleep(250);
        }
    } else {
        out(L"  [*] Looking for a running Minecraft (64-bit JVM)...\n");
        while (true) {
            if (select_best_candidate(options, &target)) {
                break;
            }
            if (GetTickCount() >= deadline) {
                out(L"  [!] No ready Minecraft process found within %.0f seconds.\n",
                    options.wait_seconds);
                out(L"      Start Minecraft first, or pass --launch.\n");
                if (!options.no_pause) pause_if_console();
                return 5;
            }
            Sleep(250);
        }
    }

    if (!is_64_bit_process(target.pid)) {
        out(L"  [!] Target process %lu is 32-bit. Slinky requires a 64-bit JVM.\n",
            static_cast<unsigned long>(target.pid));
        if (!options.no_pause) pause_if_console();
        return 6;
    }

    out(L"  [*] Target: %ls  (PID %lu)\n", target.exe_name.c_str(),
        static_cast<unsigned long>(target.pid));
    out(L"  [*] Injecting Slinky...\n");

    DWORD inject_error = 0;
    if (!inject_dll(target.pid, options.dll_path, &inject_error)) {
        out(L"  [!] Injection failed (error %lu).\n", static_cast<unsigned long>(inject_error));
        if (!options.no_pause) pause_if_console();
        return 7;
    }

    out(L"  [+] Injection successful! Slinky is loading in the game.\n");
    if (!options.no_pause && interactive) {
        countdown_and_close(5u);
    }
    return 0;
}
