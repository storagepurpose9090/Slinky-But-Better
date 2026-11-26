#include "vendor/anti_debug_tamper.hpp"

#include <cstring>
#include <intrin.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>

// Isolated reference reconstruction of loader & runtime environment checks (TASK.md Req 3 & 4).
// Never included or invoked by `libslinky.dll`.

namespace slinky::vendor {

bool check_peb_debugger_flags() noexcept {
    return IsDebuggerPresent() != FALSE;
}

bool check_kuser_shared_data() noexcept {
    // KUSER_SHARED_DATA kernel debugger status byte at 0x7FFE02D4
    const auto* kd_ptr = reinterpret_cast<const volatile uint8_t*>(static_cast<uintptr_t>(0x7FFE02D4u));
    return ((*kd_ptr) & 0x01u) != 0u;
}

bool check_win32_debugger_apis() noexcept {
    if (IsDebuggerPresent() != FALSE) {
        return true;
    }
    BOOL remote_present = FALSE;
    if (CheckRemoteDebuggerPresent(GetCurrentProcess(), &remote_present) != FALSE && remote_present != FALSE) {
        return true;
    }
    return false;
}

bool check_nt_process_debug() noexcept {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }
    using NtQueryInfoFn = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    const auto query_fn = reinterpret_cast<NtQueryInfoFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "NtQueryInformationProcess")));
    if (!query_fn) {
        return false;
    }
    ULONG_PTR debug_port = 0;
    const NTSTATUS status = query_fn(
        GetCurrentProcess(),
        ProcessDebugPort,
        &debug_port,
        static_cast<ULONG>(sizeof(debug_port)),
        nullptr);
    return NT_SUCCESS(status) && debug_port != 0;
}

bool hide_and_verify_thread_from_debugger() noexcept {
    return true;
}

bool check_kernel_debugger_and_drivers() noexcept {
    return check_kuser_shared_data();
}

bool check_hardware_breakpoints() noexcept {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(GetCurrentThread(), &ctx)) {
        return false;
    }
    return (ctx.Dr0 | ctx.Dr1 | ctx.Dr2 | ctx.Dr3) != 0;
}

bool check_ntclose_tamper_and_handle_probe() noexcept {
    return verify_ntdll_syscall_stubs();
}

bool check_virtual_machine() noexcept {
    int cpu_info[4] = {0, 0, 0, 0};
    __cpuid(cpu_info, 1);
    const bool hypervisor_bit = (static_cast<uint32_t>(cpu_info[2]) & (1u << 31)) != 0u;
    if (!hypervisor_bit) {
        return false;
    }

    int hv_leaf[4] = {0, 0, 0, 0};
    __cpuid(hv_leaf, 0x40000000);
    char vendor_id[13]{};
    std::memcpy(vendor_id + 0, &hv_leaf[1], 4);
    std::memcpy(vendor_id + 4, &hv_leaf[2], 4);
    std::memcpy(vendor_id + 8, &hv_leaf[3], 4);
    vendor_id[12] = '\0';

    // Standard Windows Hyper-V host partition reports "Microsoft Hv"
    if (std::strcmp(vendor_id, "Microsoft Hv") == 0) {
        return false;
    }
    return vendor_id[0] != '\0';
}

uint32_t compute_crc32(const void* data, size_t length, uint32_t seed) noexcept {
    uint32_t crc = ~seed;
    const auto* bytes = static_cast<const uint8_t*>(data);
    if (!bytes) {
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool verify_ntdll_syscall_stubs() noexcept {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return true;
    }
    static constexpr const char* kCheckedExports[] = {
        "ZwClose",
        "ZwQueryInformationProcess",
        "ZwProtectVirtualMemory",
        "ZwRaiseHardError",
    };
    for (const char* name : kCheckedExports) {
        const auto* fn_bytes = reinterpret_cast<const uint8_t*>(GetProcAddress(ntdll, name));
        if (!fn_bytes) {
            continue;
        }
        // Standard x64 Windows syscall stub starts with `mov r10, rcx` (0x4C 0x8B 0xD1)
        if (fn_bytes[0] != 0x4Cu || fn_bytes[1] != 0x8Bu) {
            return false;
        }
    }
    return true;
}

bool verify_pe_readonly_crc32(const void* module_base, uint32_t expected_text_crc) noexcept {
    if (!module_base || expected_text_crc == 0) {
        return true;
    }
    const auto* base = static_cast<const uint8_t*>(module_base);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::strncmp(reinterpret_cast<const char*>(sec[i].Name), ".text", 5) == 0) {
            const uint32_t actual = compute_crc32(base + sec[i].VirtualAddress, sec[i].Misc.VirtualSize);
            return actual == expected_text_crc;
        }
    }
    return true;
}

SecurityCheckReport run_all_security_checks(const void* module_base, uint32_t expected_text_crc) noexcept {
    SecurityCheckReport report{};
    report.peb_debugged             = check_peb_debugger_flags();
    report.kuser_kd_enabled         = check_kuser_shared_data();
    report.win32_api_debugged       = check_win32_debugger_apis();
    report.nt_debug_port_or_object  = check_nt_process_debug();
    report.kernel_debugger_present  = check_kernel_debugger_and_drivers();
    report.hardware_breakpoints_set = check_hardware_breakpoints();
    report.ntdll_inline_hooked      = !verify_ntdll_syscall_stubs();
    report.virtual_machine_detected = check_virtual_machine();
    report.image_crc_mismatch       = !verify_pe_readonly_crc32(module_base, expected_text_crc);
    return report;
}

bool is_debugger_or_tamper_active() noexcept {
    return run_all_security_checks().any_triggered();
}

[[noreturn]] void raise_debugger_hard_error_and_exit() noexcept {
    MessageBoxA(
        nullptr,
        "Slinky cannot be ran while a debugger is active.",
        "slinky_beta",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    ExitProcess(0xDEADu);
}

} // namespace slinky::vendor
