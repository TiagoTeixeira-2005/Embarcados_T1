#ifndef HEXDUMP_HPP
#define HEXDUMP_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

inline std::string bytes_para_hex(const uint8_t *dados, size_t tamanho) {
    static const char DIGITOS[] = "0123456789ABCDEF";

    std::string texto;
    if (dados == nullptr || tamanho == 0) {
        return texto;
    }
    texto.reserve(tamanho * 5 - 1);
    for (size_t i = 0; i < tamanho; ++i) {
        if (i > 0) {
            texto.push_back(' ');
        }
        texto.push_back('0');
        texto.push_back('x');
        texto.push_back(DIGITOS[(dados[i] >> 4) & 0x0F]);
        texto.push_back(DIGITOS[dados[i] & 0x0F]);
    }
    return texto;
}

inline std::string bytes_para_hex(const std::vector<uint8_t> &bytes) {
    return bytes_para_hex(bytes.data(), bytes.size());
}

#endif 
