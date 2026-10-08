#ifndef PROTOCOLO_MODBUS_HPP
#define PROTOCOLO_MODBUS_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "common/matricula.hpp"
#include "common/status.hpp"
#include "modbus/rtu_client.hpp"

namespace protocolo_modbus {

constexpr uint8_t FUNCAO_SOLICITA = 0x23;
constexpr uint8_t FUNCAO_ENVIA = 0x16;

constexpr uint8_t SUB_SOLICITA_INTEIRO = 0xA1;
constexpr uint8_t SUB_SOLICITA_FLOAT = 0xA2;
constexpr uint8_t SUB_SOLICITA_STRING = 0xA3;
constexpr uint8_t SUB_ENVIA_INTEIRO = 0xB1;
constexpr uint8_t SUB_ENVIA_FLOAT = 0xB2;
constexpr uint8_t SUB_ENVIA_STRING = 0xB3;
constexpr size_t TAMANHO_MAX_STRING_ENVIO = 237;

struct Contexto {
    RtuClient *cliente = nullptr;
    Matricula matricula{};
    uint8_t endereco = 0x01; 
};
std::vector<uint8_t> montar_requisicao(const Contexto &ctx, uint8_t funcao, uint8_t subcodigo,
                                       const std::vector<uint8_t> &dados);
Resultado<std::vector<uint8_t>> sondar(const Contexto &ctx, uint8_t funcao, uint8_t subcodigo,
                                       const std::vector<uint8_t> &dados);

Resultado<int32_t> solicita_inteiro(const Contexto &ctx);       // 0x23 0xA1
Resultado<float> solicita_float(const Contexto &ctx);           // 0x23 0xA2
Resultado<std::string> solicita_string(const Contexto &ctx);    // 0x23 0xA3
Resultado<int32_t> envia_inteiro(const Contexto &ctx, int32_t valor);          // 0x16 0xB1
Resultado<float> envia_float(const Contexto &ctx, float valor);                // 0x16 0xB2
Resultado<std::string> envia_string(const Contexto &ctx, const std::string &texto); // 0x16 0xB3

} 

#endif 
