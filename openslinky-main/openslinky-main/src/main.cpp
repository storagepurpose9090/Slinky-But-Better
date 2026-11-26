#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>

#include "core/client.hpp"
#include "modules/module_interop.hpp"
#include "util/filesystem.hpp"

// NOTE: All vendor authentication, licensing, HWID, AES-128, HTTPS, and anti-debug/anti-tamper
// routines are preserved as reference source code in `src/vendor/` (`slinky_vendor` static library),
// and are intentionally NOT included, linked, or called anywhere in `libslinky.dll`.

namespace {

HMODULE g_dll_instance = nullptr;
std::atomic<uint8_t> g_init_started{0};

DWORD WINAPI client_init_worker(LPVOID param) noexcept {
    auto hmod = static_cast<HMODULE>(param);
    const int err = slinky::core::initialize_client(hmod);
    if (err != 0) {
        slinky::fs::show_error_dialog("Initialization failed: %u", static_cast<unsigned int>(err));
        FreeLibraryAndExitThread(hmod, static_cast<DWORD>(err));
    }
    return 0;
}

void start_client_async(HMODULE hmod) noexcept {
    uint8_t expected = 0;
    if (g_init_started.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        HANDLE thread = CreateThread(nullptr, 0, &client_init_worker, hmod, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    } else {
        slinky::modules::notifications_reset_welcome_toast();
    }
}

} // namespace

extern "C" __declspec(dllexport) void slinky_init() {
    start_client_async(g_dll_instance);
}

BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_dll_instance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        start_client_async(hinstDLL);
    }
    return TRUE;
}
