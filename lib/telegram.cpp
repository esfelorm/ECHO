#include "telegram.h"
#include <windows.h>
#include <wininet.h>
#include <cstdio>
#include <ctime>

#pragma comment(lib, "wininet.lib")

AutoHopTelegram::AutoHopTelegram() {
    bot_token  = "";
    chat_id    = "";
    configured = (!bot_token.empty() && !chat_id.empty());
}

AutoHopTelegram::~AutoHopTelegram() {}

bool AutoHopTelegram::is_configured() const {
    return configured;
}

int AutoHopTelegram::get_random_port() {
    srand((unsigned)time(NULL) ^ GetCurrentProcessId());
    return 20000 + (rand() % 40000);
}

static std::string url_encode(const std::string& s) {
    std::string out;
    char buf[8];
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            sprintf_s(buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

void AutoHopTelegram::send(const std::string& text) {
    if (!configured) return;
    if (bot_token.empty() || chat_id.empty()) return;

    std::string url = "https://api.telegram.org/bot" + bot_token +
                      "/sendMessage?chat_id=" + chat_id +
                      "&text=" + url_encode(text);

    HINTERNET hNet = InternetOpenA("ECHO", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hNet) return;
    HINTERNET hUrl = InternetOpenUrlA(hNet, url.c_str(), NULL, 0,
                                      INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (hUrl) {
        char buf[1024];
        DWORD read = 0;
        while (InternetReadFile(hUrl, buf, sizeof(buf) - 1, &read) && read > 0) {
            buf[read] = 0;
        }
        InternetCloseHandle(hUrl);
    }
    InternetCloseHandle(hNet);
}

void AutoHopTelegram::send_startup_message(int port) {
    char msg[256];
    sprintf_s(msg, "[ECHO-V3] Server started on port %d", port);
    send(msg);
}

void AutoHopTelegram::send_hop_message(int old_port, int new_port) {
    char msg[256];
    sprintf_s(msg, "[ECHO-V3] Hopped from %d to %d", old_port, new_port);
    send(msg);
}