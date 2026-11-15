/* Paper Cut.exe: what the player starts on Windows. It starts the US build
 * (ttyd-us.exe, beside it) with the same arguments; that one shows the
 * launcher (port/settings/launcher_ui.cpp), which starts the build of the
 * disc's version (ttyd-jp.exe, ttyd-us.exe, ttyd-eu.exe). The builds keep
 * those names: code mods are linked against them.
 *
 * It waits for that start: one that the graphics driver ended while a graphics
 * API started (exit code PORT_EXIT_GPU_START, port/include/port/gpu_start.h) is
 * started again, and passes that API over; when every API has, a plain message
 * says what to do (a player's 2020 Intel driver crashed in Direct3D 12, so
 * nothing ever opened). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#define PORT_GPU_START_EXIT_ONLY
#include "../include/port/gpu_start.h"

/* Direct3D 12, Vulkan, Direct3D 11: a start each, then the message */
#define GPU_START_TRIES 3

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR args, int show) {
    static wchar_t exe[32768], line[32768];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t* slash;
    DWORD code = 0;
    int tries;
    DWORD n = GetModuleFileNameW(NULL, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    (void)instance, (void)previous, (void)show;
    if (n == 0 || n >= sizeof(exe) / sizeof(exe[0])) {
        return 1;
    }
    slash = wcsrchr(exe, L'\\');
    if (slash == NULL) {
        return 1;
    }
    slash[1] = L'\0';
    if (wcslen(exe) + 16 >= sizeof(exe) / sizeof(exe[0])) {
        return 1;
    }
    wcscat(exe, L"ttyd-us.exe");
    for (tries = 0; tries < GPU_START_TRIES; tries++) {
        swprintf(line, sizeof(line) / sizeof(line[0]), L"\"%ls\" %ls", exe, args != NULL ? args : L"");
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        if (!CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            MessageBoxW(NULL,
                        L"Paper Cut could not start ttyd-us.exe. It should be in the same folder as Paper Cut.exe.",
                        L"Paper Cut", MB_OK | MB_ICONERROR);
            return 1;
        }
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        if (!GetExitCodeProcess(pi.hProcess, &code)) {
            code = 0;
        }
        CloseHandle(pi.hProcess);
        if (code != PORT_EXIT_GPU_START) {
            return (int)code;
        }
    }
    MessageBoxW(NULL,
                L"Paper Cut could not start its graphics: the graphics driver stopped it each time (with Direct3D 12, "
                L"Vulkan and Direct3D 11).\n\n"
                L"Update your graphics driver from the maker of your graphics card or PC (Intel, AMD, NVIDIA), then "
                L"start Paper Cut again. If it still closes, please report it with last_crash.txt from the ttyd "
                L"folder in %APPDATA%.",
                L"Paper Cut", MB_OK | MB_ICONERROR);
    return 1;
}
