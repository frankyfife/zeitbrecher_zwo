// Twofish block cipher (128-bit keys only) and the CTR mode CryEngine uses
// (libtomcrypt ctr_start with CTR_COUNTER_LITTLE_ENDIAN over the whole block).
// Written from the published specification (Schneier et al., 1998).
#pragma once
#include <cstddef>
#include <cstdint>

class Twofish128 {
public:
    explicit Twofish128(const uint8_t key[16]);
    void encrypt_block(const uint8_t in[16], uint8_t out[16]) const;

private:
    uint32_t g(uint32_t x) const;
    uint32_t K_[40];
    uint32_t G_[4][256];
};

// XORs data[0..size) with the keystream E(iv), E(iv+1), ... where the
// counter is the 16-byte IV read as one little-endian 128-bit integer.
// Decryption and encryption are the same operation.
void twofish_ctr_le(const uint8_t key[16], const uint8_t iv[16], uint8_t* data, size_t size);
