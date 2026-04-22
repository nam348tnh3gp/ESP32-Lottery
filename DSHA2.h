#ifndef DSHA2_H
#define DSHA2_H

#include <Arduino.h>

// Fallback cho compiler cũ không hỗ trợ __builtin_rotateright32
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if __has_builtin(__builtin_rotateright32)
  #define ROTR32 __builtin_rotateright32
#else
  static inline uint32_t ROTR32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
  }
#endif

class DSHA256 {
public:
    static const size_t OUTPUT_SIZE = 32;

    DSHA256() {
        bytes = 0;
        initialize(s);
    }

    DSHA256 &write(const unsigned char *data, size_t len) {
        size_t bufsize = bytes % 64;
        if (bufsize && bufsize + len >= 64) {
            memcpy(buf + bufsize, data, 64 - bufsize);
            bytes += 64 - bufsize;
            data += 64 - bufsize;
            len  -= 64 - bufsize;
            transform(s, buf);
            bufsize = 0;
        }
        while (len >= 64) {
            transform(s, data);
            bytes += 64;
            data += 64;
            len -= 64;
        }
        if (len > 0) {
            memcpy(buf + bufsize, data, len);
            bytes += len;
        }
        return *this;
    }

    void finalize(unsigned char hash[OUTPUT_SIZE]) {
        const unsigned char pad[64] = {0x80};
        unsigned char sizedesc[8];
        writeBE64(sizedesc, bytes << 3);
        write(pad, 1 + ((119 - (bytes % 64)) % 64));
        write(sizedesc, 8);
        for (int i = 0; i < 8; i++) {
            writeBE32(hash + i * 4, s[i]);
        }
    }

    DSHA256 &reset() {
        bytes = 0;
        initialize(s);
        return *this;
    }

    DSHA256 &warmup() {
        uint8_t warmup[32];
        this->write((uint8_t *)"warmupwarmupwarmupwarmup12", 32).finalize(warmup);
        return *this;
    }

    // Hàm băm 1 lần cho block header (80 byte) - tiện cho mining
    void hashBlockHeader(const unsigned char header[80], unsigned char hash[OUTPUT_SIZE]) {
        reset();
        write(header, 80);
        finalize(hash);
        
        // Bitcoin yêu cầu double SHA-256
        unsigned char hash2[OUTPUT_SIZE];
        reset();
        write(hash, OUTPUT_SIZE);
        finalize(hash2);
        memcpy(hash, hash2, OUTPUT_SIZE);
    }

private:
    uint32_t s[8];
    unsigned char buf[64];
    uint64_t bytes;

    static const uint32_t K[64];

    // SHA-256 functions
    static inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (~x & z);
    }
    static inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }
    static inline uint32_t Sigma0(uint32_t x) {
        return ROTR32(x, 2) ^ ROTR32(x, 13) ^ ROTR32(x, 22);
    }
    static inline uint32_t Sigma1(uint32_t x) {
        return ROTR32(x, 6) ^ ROTR32(x, 11) ^ ROTR32(x, 25);
    }
    static inline uint32_t sigma0(uint32_t x) {
        return ROTR32(x, 7) ^ ROTR32(x, 18) ^ (x >> 3);
    }
    static inline uint32_t sigma1(uint32_t x) {
        return ROTR32(x, 17) ^ ROTR32(x, 19) ^ (x >> 10);
    }

    void initialize(uint32_t *s) {
        s[0] = 0x6a09e667ul;
        s[1] = 0xbb67ae85ul;
        s[2] = 0x3c6ef372ul;
        s[3] = 0xa54ff53aul;
        s[4] = 0x510e527ful;
        s[5] = 0x9b05688cul;
        s[6] = 0x1f83d9abul;
        s[7] = 0x5be0cd19ul;
    }

    void transform(uint32_t *s, const unsigned char *chunk) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = readBE32(chunk + i * 4);
        }
        for (int i = 16; i < 64; i++) {
            w[i] = sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16];
        }

        uint32_t a = s[0], b = s[1], c = s[2], d = s[3];
        uint32_t e = s[4], f = s[5], g = s[6], h = s[7];

        for (int i = 0; i < 64; i++) {
            uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = Sigma0(a) + Maj(a, b, c);
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }

        s[0] += a; s[1] += b; s[2] += c; s[3] += d;
        s[4] += e; s[5] += f; s[6] += g; s[7] += h;
    }

    static inline uint32_t readBE32(const unsigned char *ptr) {
        uint32_t val;
        memcpy(&val, ptr, 4); // Tránh alignment issue
        return __builtin_bswap32(val);
    }

    static inline void writeBE32(unsigned char *ptr, uint32_t x) {
        uint32_t val = __builtin_bswap32(x);
        memcpy(ptr, &val, 4);
    }

    static inline void writeBE64(unsigned char *ptr, uint64_t x) {
        uint64_t val = __builtin_bswap64(x);
        memcpy(ptr, &val, 8);
    }
};

// SHA-256 constants
const uint32_t DSHA256::K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#endif
