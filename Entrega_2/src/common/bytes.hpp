#ifndef BYTES_HPP
#define BYTES_HPP

#include <cstdint>
#include <cstring>
#include <vector>

static_assert(sizeof(float) == 4, "float precisa ter 32 bits (IEEE 754)");

inline void escrever_u16_le(std::vector<uint8_t> &destino, uint16_t valor) {
    destino.push_back(static_cast<uint8_t>(valor & 0xFF));
    destino.push_back(static_cast<uint8_t>((valor >> 8) & 0xFF));
}

inline void escrever_u16_be(std::vector<uint8_t> &destino, uint16_t valor) {
    destino.push_back(static_cast<uint8_t>((valor >> 8) & 0xFF));
    destino.push_back(static_cast<uint8_t>(valor & 0xFF));
}

inline void escrever_u32_le(std::vector<uint8_t> &destino, uint32_t valor) {
    for (int i = 0; i < 4; ++i) {
        destino.push_back(static_cast<uint8_t>((valor >> (8 * i)) & 0xFF));
    }
}

inline void escrever_i32_le(std::vector<uint8_t> &destino, int32_t valor) {
    uint32_t bits;
    std::memcpy(&bits, &valor, sizeof(bits));
    escrever_u32_le(destino, bits);
}

inline void escrever_float_le(std::vector<uint8_t> &destino, float valor) {
    uint32_t bits;
    std::memcpy(&bits, &valor, sizeof(bits));
    escrever_u32_le(destino, bits);
}

inline uint16_t ler_u16_le(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint16_t ler_u16_be(const uint8_t *p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint32_t ler_u32_le(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t ler_i32_le(const uint8_t *p) {
    const uint32_t bits = ler_u32_le(p);
    int32_t valor;
    std::memcpy(&valor, &bits, sizeof(valor));
    return valor;
}

inline float ler_float_le(const uint8_t *p) {
    const uint32_t bits = ler_u32_le(p);
    float valor;
    std::memcpy(&valor, &bits, sizeof(valor));
    return valor;
}

#endif 
