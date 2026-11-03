/* Paper Cut.exe: what the player starts on Windows. It starts the US build
 * (ttyd-us.exe, beside it) with the same arguments; that one shows the
 * launcher (port/settings/launcher_ui.cpp), which starts the build of the
 * disc's version (ttyd-jp.exe, ttyd-us.exe, ttyd-eu.exe). The builds keep
 * those names: code mods are linked against them. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR args, int show) {
    static wchar_t exe[32768], line[32768];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t* slash;
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
    swprintf(line, sizeof(line) / sizeof(line[0]), L"\"%ls\" %ls", exe, args != NULL ? args : L"");
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxW(NULL, L"Paper Cut could not start ttyd-us.exe. It should be in the same folder as Paper Cut.exe.",
                    L"Paper Cut", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
