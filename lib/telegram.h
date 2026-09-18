#pragma once
#include <string>
#include <cstdlib>

class AutoHopTelegram {
public:
    AutoHopTelegram();
    ~AutoHopTelegram();

    bool is_configured() const;
    int  get_random_port();
    void send_startup_message(int port);
    void send_hop_message(int old_port, int new_port);

private:
    std::string bot_token;
    std::string chat_id;
    bool        configured;
    void        send(const std::string& text);
};