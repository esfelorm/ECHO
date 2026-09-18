#include "crypto.h"
#include <cstring>

static inline uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

Crypto::Crypto(const unsigned char* key, int keyLen) { init(key, keyLen); }

Crypto::~Crypto() {
    memset(state, 0, sizeof(state));
    memset(working, 0, sizeof(working));
    memset(keyStream, 0, sizeof(keyStream));
    memset(key, 0, sizeof(key));
    memset(nonce, 0, sizeof(nonce));
}

void Crypto::init(const unsigned char* k, int keyLen) {
    memset(key, 0, sizeof(key));
    if (keyLen > 32) keyLen = 32;
    memcpy(key, k, keyLen);

    state[0] = 0x61707865;
    state[1] = 0x3320646e;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;

    for (int i = 0; i < 8; i++) {
        state[4 + i] = (uint32_t)key[i*4] |
                       ((uint32_t)key[i*4+1] << 8) |
                       ((uint32_t)key[i*4+2] << 16) |
                       ((uint32_t)key[i*4+3] << 24);
    }

    memset(nonce, 0, sizeof(nonce));
    state[12] = 0;
    state[13] = 0;
    state[14] = 0;
    state[15] = 0;

    counter = 0;
    keyStreamPos = 64;
    memcpy(working, state, sizeof(state));
}

void Crypto::quarter_round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    a += b; d ^= a; d = rotl32(d, 16);
    c += d; b ^= c; b = rotl32(b, 12);
    a += b; d ^= a; d = rotl32(d, 8);
    c += d; b ^= c; b = rotl32(b, 7);
}

void Crypto::chacha20_block(uint32_t out[16], const uint32_t in[16]) {
    uint32_t x[16];
    memcpy(x, in, sizeof(x));
    for (int i = 0; i < 10; i++) {
        quarter_round(x[0], x[4], x[8],  x[12]);
        quarter_round(x[1], x[5], x[9],  x[13]);
        quarter_round(x[2], x[6], x[10], x[14]);
        quarter_round(x[3], x[7], x[11], x[15]);
        quarter_round(x[0], x[5], x[10], x[15]);
        quarter_round(x[1], x[6], x[11], x[12]);
        quarter_round(x[2], x[7], x[8],  x[13]);
        quarter_round(x[3], x[4], x[9],  x[14]);
    }
    for (int i = 0; i < 16; i++) out[i] = x[i] + in[i];
}

void Crypto::reset() {
    counter = 0;
    keyStreamPos = 64;
    memcpy(working, state, sizeof(state));
}

void Crypto::encrypt(unsigned char* data, int len) {
    for (int i = 0; i < len; i++) {
        if (keyStreamPos >= 64) {
            working[12] = counter++;
            uint32_t block[16];
            chacha20_block(block, working);
            memcpy(keyStream, block, 64);
            keyStreamPos = 0;
        }
        data[i] ^= keyStream[keyStreamPos++];
    }
}

void Crypto::decrypt(unsigned char* data, int len) { encrypt(data, len); }