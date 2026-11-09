#include "twofish.h"

#include <cstring>
#include <mutex>

namespace {

const uint8_t kQ0t[4][16] = {
    {0x8, 0x1, 0x7, 0xD, 0x6, 0xF, 0x3, 0x2, 0x0, 0xB, 0x5, 0x9, 0xE, 0xC, 0xA, 0x4},
    {0xE, 0xC, 0xB, 0x8, 0x1, 0x2, 0x3, 0x5, 0xF, 0x4, 0xA, 0x6, 0x7, 0x0, 0x9, 0xD},
    {0xB, 0xA, 0x5, 0xE, 0x6, 0xD, 0x9, 0x0, 0xC, 0x8, 0xF, 0x3, 0x2, 0x4, 0x7, 0x1},
    {0xD, 0x7, 0xF, 0x4, 0x1, 0x2, 0x6, 0xE, 0x9, 0xB, 0x3, 0x0, 0x8, 0x5, 0xC, 0xA}};
const uint8_t kQ1t[4][16] = {
    {0x2, 0x8, 0xB, 0xD, 0xF, 0x7, 0x6, 0xE, 0x3, 0x1, 0x9, 0x4, 0x0, 0xA, 0xC, 0x5},
    {0x1, 0xE, 0x2, 0xB, 0x4, 0xC, 0x3, 0x7, 0x6, 0xD, 0xA, 0x5, 0xF, 0x9, 0x0, 0x8},
    {0x4, 0xC, 0x7, 0x5, 0x1, 0x6, 0x9, 0xA, 0x0, 0xE, 0xD, 0x8, 0x2, 0xB, 0x3, 0xF},
    {0xB, 0x9, 0x5, 0x1, 0xC, 0x3, 0xD, 0xE, 0x6, 0x4, 0x7, 0xF, 0x2, 0x0, 0x8, 0xA}};
const uint8_t kMDS[4][4] = {
    {0x01, 0xEF, 0x5B, 0x5B}, {0x5B, 0xEF, 0xEF, 0x01}, {0xEF, 0x5B, 0x01, 0xEF}, {0xEF, 0x01, 0xEF, 0x5B}};
const uint8_t kRS[4][8] = {{0x01, 0xA4, 0x55, 0x87, 0x5A, 0x58, 0xDB, 0x9E},
                           {0xA4, 0x56, 0x82, 0xF3, 0x1E, 0xC6, 0x68, 0xE5},
                           {0x02, 0xA1, 0xFC, 0xC1, 0x47, 0xAE, 0x3D, 0x19},
                           {0xA4, 0x55, 0x87, 0x5A, 0x58, 0xDB, 0x9E, 0x03}};

uint8_t q0[256], q1[256];
uint32_t MT[4][256];
std::once_flag g_tables_once;

inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint8_t ror4(uint8_t x, int n) { return uint8_t(((x >> n) | (x << (4 - n))) & 0xF); }

uint8_t gf_mul(uint8_t a, uint8_t b, uint16_t poly) {
    uint16_t aa = a, r = 0;
    while (b) {
        if (b & 1) r ^= aa;
        aa <<= 1;
        if (aa & 0x100) aa ^= poly;
        b >>= 1;
    }
    return uint8_t(r);
}

void make_q(const uint8_t t[4][16], uint8_t* q) {
    for (int x = 0; x < 256; ++x) {
        uint8_t a0 = uint8_t(x >> 4), b0 = uint8_t(x & 15);
        uint8_t a1 = a0 ^ b0, b1 = uint8_t((a0 ^ ror4(b0, 1) ^ (8 * a0)) & 15);
        uint8_t a2 = t[0][a1], b2 = t[1][b1];
        uint8_t a3 = a2 ^ b2, b3 = uint8_t((a2 ^ ror4(b2, 1) ^ (8 * a2)) & 15);
        uint8_t a4 = t[2][a3], b4 = t[3][b3];
        q[x] = uint8_t(16 * b4 + a4);
    }
}

void init_tables() {
    make_q(kQ0t, q0);
    make_q(kQ1t, q1);
    for (int i = 0; i < 4; ++i)
        for (int b = 0; b < 256; ++b) {
            uint32_t v = 0;
            for (int r = 0; r < 4; ++r) v |= uint32_t(gf_mul(kMDS[r][i], uint8_t(b), 0x169)) << (8 * r);
            MT[i][b] = v;
        }
}

inline uint8_t byte_of(uint32_t w, int i) { return uint8_t(w >> (8 * i)); }

// The byte permutations of h() for a 128-bit key (k = 2), before the MDS.
void h_bytes(uint32_t x, const uint32_t L[2], uint8_t y[4]) {
    y[0] = q1[q0[q0[byte_of(x, 0)] ^ byte_of(L[1], 0)] ^ byte_of(L[0], 0)];
    y[1] = q0[q0[q1[byte_of(x, 1)] ^ byte_of(L[1], 1)] ^ byte_of(L[0], 1)];
    y[2] = q1[q1[q0[byte_of(x, 2)] ^ byte_of(L[1], 2)] ^ byte_of(L[0], 2)];
    y[3] = q0[q1[q1[byte_of(x, 3)] ^ byte_of(L[1], 3)] ^ byte_of(L[0], 3)];
}

uint32_t h_full(uint32_t x, const uint32_t L[2]) {
    uint8_t y[4];
    h_bytes(x, L, y);
    return MT[0][y[0]] ^ MT[1][y[1]] ^ MT[2][y[2]] ^ MT[3][y[3]];
}

}  // namespace

Twofish128::Twofish128(const uint8_t key[16]) {
    std::call_once(g_tables_once, init_tables);
    uint32_t M[4];
    for (int i = 0; i < 4; ++i) std::memcpy(&M[i], key + 4 * i, 4);
    const uint32_t Me[2] = {M[0], M[2]}, Mo[2] = {M[1], M[3]};

    uint32_t S[2];
    for (int i = 0; i < 2; ++i) {
        uint32_t s = 0;
        for (int r = 0; r < 4; ++r) {
            uint8_t a = 0;
            for (int c = 0; c < 8; ++c) a ^= gf_mul(kRS[r][c], key[8 * i + c], 0x14D);
            s |= uint32_t(a) << (8 * r);
        }
        S[1 - i] = s;  // the S vector is used in reverse order
    }

    const uint32_t rho = 0x01010101u;
    for (int i = 0; i < 20; ++i) {
        uint32_t A = h_full(2 * i * rho, Me);
        uint32_t B = rol(h_full((2 * i + 1) * rho, Mo), 8);
        K_[2 * i] = A + B;
        K_[2 * i + 1] = rol(A + 2 * B, 9);
    }
    for (int b = 0; b < 256; ++b) {
        uint8_t y[4];
        h_bytes(uint32_t(b) * rho, S, y);
        for (int i = 0; i < 4; ++i) G_[i][b] = MT[i][y[i]];
    }
}

uint32_t Twofish128::g(uint32_t x) const {
    return G_[0][byte_of(x, 0)] ^ G_[1][byte_of(x, 1)] ^ G_[2][byte_of(x, 2)] ^ G_[3][byte_of(x, 3)];
}

void Twofish128::encrypt_block(const uint8_t in[16], uint8_t out[16]) const {
    uint32_t R[4];
    for (int i = 0; i < 4; ++i) {
        std::memcpy(&R[i], in + 4 * i, 4);
        R[i] ^= K_[i];
    }
    for (int r = 0; r < 16; ++r) {
        uint32_t T0 = g(R[0]);
        uint32_t T1 = g(rol(R[1], 8));
        uint32_t F0 = T0 + T1 + K_[2 * r + 8];
        uint32_t F1 = T0 + 2 * T1 + K_[2 * r + 9];
        uint32_t n2 = ror(R[2] ^ F0, 1);
        uint32_t n3 = rol(R[3], 1) ^ F1;
        R[2] = R[0];
        R[3] = R[1];
        R[0] = n2;
        R[1] = n3;
    }
    const uint32_t C[4] = {R[2] ^ K_[4], R[3] ^ K_[5], R[0] ^ K_[6], R[1] ^ K_[7]};
    std::memcpy(out, C, 16);
}

void twofish_ctr_le(const uint8_t key[16], const uint8_t iv[16], uint8_t* data, size_t size) {
    Twofish128 tf(key);
    uint8_t ctr[16], ks[16];
    std::memcpy(ctr, iv, 16);
    for (size_t off = 0; off < size; off += 16) {
        tf.encrypt_block(ctr, ks);
        size_t n = size - off < 16 ? size - off : 16;
        for (size_t j = 0; j < n; ++j) data[off + j] ^= ks[j];
        for (int j = 0; j < 16; ++j)
            if (++ctr[j]) break;
    }
}
