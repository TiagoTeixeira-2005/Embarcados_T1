#ifndef MATRICULA_HPP
#define MATRICULA_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "common/status.hpp"

constexpr size_t TAMANHO_MATRICULA = 6;

using Matricula = std::array<uint8_t, TAMANHO_MATRICULA>;

inline Resultado<Matricula> matricula_de_texto(const std::string &texto) {
    if (texto.size() != TAMANHO_MATRICULA) {
        return Resultado<Matricula>::com_falha(Status::falha(
            Erro::ArgumentoInvalido, "a matricula deve ter exatamente 6 digitos"));
    }
    Matricula matricula{};
    for (size_t i = 0; i < TAMANHO_MATRICULA; ++i) {
        if (texto[i] < '0' || texto[i] > '9') {
            return Resultado<Matricula>::com_falha(Status::falha(
                Erro::ArgumentoInvalido, "a matricula deve conter apenas digitos 0-9"));
        }
        matricula[i] = static_cast<uint8_t>(texto[i] - '0');
    }
    return Resultado<Matricula>::com_valor(matricula);
}

inline std::string matricula_para_texto(const Matricula &matricula) {
    std::string texto;
    for (uint8_t digito : matricula) {
        texto.push_back(static_cast<char>('0' + digito));
    }
    return texto;
}
inline void anexar_matricula(std::vector<uint8_t> &pacote, const Matricula &matricula) {
    pacote.insert(pacote.end(), matricula.begin(), matricula.end());
}
inline uint8_t ultimo_digito(const Matricula &matricula) {
    return matricula[TAMANHO_MATRICULA - 1];
}

#endif 
