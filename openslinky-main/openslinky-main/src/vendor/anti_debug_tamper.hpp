#pragma once

#include <cstddef>
#include <cstdint>

// NOTE: Isolated vendor anti-debug, anti-VM, and anti-tamper routines (Requirement 3 & 4 of TASK.md).
// Restored from the VMProtect 3.8 loader/runtime checks and Slinky's periodic runtime guard
// (`sub_18006E600` -> `sub_180158D60`) so readers can inspect every check performed by `slinky_beta`.
// NEVER #include or call any symbol from `slinky::vendor` in the active client (`libslinky.dll`).

namespace slinky::vendor {

struct SecurityCheckReport {
    bool peb_debugged = false;
    bool kuser_kd_enabled = false;
    bool win32_api_debugged = false;
    bool nt_debug_port_or_object = false;
    bool debug_filter_hooked = false;
    bool kernel_debugger_present = false;
    bool hardware_breakpoints_set = false;
    bool ntdll_inline_hooked = false;
    bool virtual_machine_detected = false;
    bool image_crc_mismatch = false;

    [[nodiscard]] constexpr bool any_triggered() const noexcept {
        return peb_debugged ||
               kuser_kd_enabled ||
               win32_api_debugged ||
               nt_debug_port_or_object ||
               debug_filter_hooked ||
               kernel_debugger_present ||
               hardware_breakpoints_set ||
               ntdll_inline_hooked ||
               virtual_machine_detected ||
               image_crc_mismatch;
    }
};

// Individual inspection primitives reconstructed from VMProtect 3.8 & Slinky runtime guards
[[nodiscard]] bool check_peb_debugger_flags() noexcept;
[[nodiscard]] bool check_kuser_shared_data() noexcept;
[[nodiscard]] bool check_win32_debugger_apis() noexcept;
[[nodiscard]] bool check_nt_process_debug() noexcept;
[[nodiscard]] bool hide_and_verify_thread_from_debugger() noexcept;
[[nodiscard]] bool check_kernel_debugger_and_drivers() noexcept;
[[nodiscard]] bool check_hardware_breakpoints() noexcept;
[[nodiscard]] bool check_ntclose_tamper_and_handle_probe() noexcept;
[[nodiscard]] bool check_virtual_machine() noexcept;
[[nodiscard]] uint32_t compute_crc32(const void* data, size_t length, uint32_t seed = 0) noexcept;
[[nodiscard]] bool verify_ntdll_syscall_stubs() noexcept;
[[nodiscard]] bool verify_pe_readonly_crc32(const void* module_base, uint32_t expected_text_crc) noexcept;

// Runs the complete suite of loader/runtime checks and populates `out_report` (optional)
[[nodiscard]] SecurityCheckReport run_all_security_checks(
    const void* module_base = nullptr,
    uint32_t expected_text_crc = 0) noexcept;

// Matches `sub_18006E600` -> `sub_180158D60` (`VMProtectIsDebuggerPresent(TRUE)` + VM check)
[[nodiscard]] bool is_debugger_or_tamper_active() noexcept;

// Displays the VMProtect `NtRaiseHardError(0x50000018)` modal dialog
// ("Slinky cannot be ran while a debugger is active.") and terminates on violation.
[[noreturn]] void raise_debugger_hard_error_and_exit() noexcept;

} // namespace slinky::vendor
