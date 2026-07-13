#ifndef WS_HANDSHAKE_H
#define WS_HANDSHAKE_H

// WebSocket(RFC 6455) 핸드셰이크에 필요한 SHA1 + Base64를 외부 의존성 없이 직접 구현한다.
// (이 환경에는 OpenSSL 개발 헤더가 설치되어 있지 않음)

#include <cstdint>
#include <string>

namespace ws_internal {

struct Sha1State { uint32_t h[5]; };

inline uint32_t rol(uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }

inline void sha1_process_block(Sha1State& st, const uint8_t* block)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
    {
        w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
               (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
    }
    for (int i = 16; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = st.h[0], b = st.h[1], c = st.h[2], d = st.h[3], e = st.h[4];

    for (int i = 0; i < 80; i++)
    {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }

        uint32_t temp = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = temp;
    }

    st.h[0] += a; st.h[1] += b; st.h[2] += c; st.h[3] += d; st.h[4] += e;
}

} // namespace ws_internal

// input을 SHA1 해시한 20바이트를 out에 채운다.
inline void sha1(const std::string& input, uint8_t out[20])
{
    using namespace ws_internal;
    Sha1State st{ {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0} };

    uint64_t bit_len = uint64_t(input.size()) * 8;

    std::string msg = input;
    msg += char(0x80);
    while (msg.size() % 64 != 56) msg += char(0x00);
    for (int i = 7; i >= 0; i--)
        msg += char((bit_len >> (i * 8)) & 0xFF);

    for (size_t off = 0; off < msg.size(); off += 64)
        sha1_process_block(st, (const uint8_t*)msg.data() + off);

    for (int i = 0; i < 5; i++)
    {
        out[i * 4]     = (st.h[i] >> 24) & 0xFF;
        out[i * 4 + 1] = (st.h[i] >> 16) & 0xFF;
        out[i * 4 + 2] = (st.h[i] >> 8) & 0xFF;
        out[i * 4 + 3] = st.h[i] & 0xFF;
    }
}

inline std::string base64_encode(const uint8_t* data, size_t len)
{
    static const char* table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= len)
    {
        uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += table[n & 0x3F];
        i += 3;
    }

    size_t rem = len - i;
    if (rem == 1)
    {
        uint32_t n = uint32_t(data[i]) << 16;
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += "==";
    }
    else if (rem == 2)
    {
        uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += "=";
    }

    return out;
}

// WebSocket 핸드셰이크 응답의 Sec-WebSocket-Accept 값을 계산한다. (RFC 6455)
inline std::string compute_ws_accept_key(const std::string& client_key)
{
    static const char* WS_MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[20];
    sha1(client_key + WS_MAGIC, digest);
    return base64_encode(digest, 20);
}

#endif
