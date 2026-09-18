#pragma once
#include <cstring>
#include <cstdint>

class Crypto {
public:
    Crypto(const unsigned char* key, int keyLen);
    ~Crypto();
    void encrypt(unsigned char* data, int len);
    void decrypt(unsigned char* data, int len);
    void reset();

private:
    void init(const unsigned char* key, int keyLen);
    void chacha20_block(uint32_t out[16], const uint32_t in[16]);
    void quarter_round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d);

    uint32_t state[16];
    uint32_t working[16];
    unsigned char keyStream[64];
    int keyStreamPos;
    uint32_t counter;
    unsigned char key[32];
    unsigned char nonce[12];
};