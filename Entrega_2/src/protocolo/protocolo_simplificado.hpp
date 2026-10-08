#ifndef PROTOCOLO_SIMPLIFICADO_HPP
#define PROTOCOLO_SIMPLIFICADO_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/matricula.hpp"
#include "common/status.hpp"
#include "common/trace.hpp"
#include "uart/uart.hpp"

namespace protocolo_simplificado {

constexpr uint8_t CMD_SOLICITA_INTEIRO = 0xA1;
constexpr uint8_t CMD_SOLICITA_FLOAT = 0xA2;
constexpr uint8_t CMD_SOLICITA_STRING = 0xA3;
constexpr uint8_t CMD_ENVIA_INTEIRO = 0xB1;
constexpr uint8_t CMD_ENVIA_FLOAT = 0xB2;
constexpr uint8_t CMD_ENVIA_STRING = 0xB3;
constexpr size_t TAMANHO_MAX_STRING_ENVIO = 237;

struct Contexto {
    PortaUart *porta = nullptr;
    Matricula matricula{};
    int timeout_ms = 300;
    TraceFn trace; 
};
std::vector<uint8_t> montar_solicitacao(uint8_t comando, const Matricula &matricula);
std::vector<uint8_t> montar_envio_inteiro(int32_t valor, const Matricula &matricula);
std::vector<uint8_t> montar_envio_float(float valor, const Matricula &matricula);
std::vector<uint8_t> montar_envio_string(const std::string &texto, const Matricula &matricula);

Resultado<int32_t> solicita_inteiro(const Contexto &ctx);       // 0xA1
Resultado<float> solicita_float(const Contexto &ctx);           // 0xA2
Resultado<std::string> solicita_string(const Contexto &ctx);    // 0xA3
Resultado<int32_t> envia_inteiro(const Contexto &ctx, int32_t valor);          // 0xB1
Resultado<float> envia_float(const Contexto &ctx, float valor);                // 0xB2
Resultado<std::string> envia_string(const Contexto &ctx, const std::string &texto); // 0xB3

} 

#endif 
