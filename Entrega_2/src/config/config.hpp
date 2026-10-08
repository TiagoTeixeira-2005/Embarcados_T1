#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <string>

#include "common/matricula.hpp"
#include "common/status.hpp"

struct Configuracao {
    std::string uart_dispositivo = "/dev/serial0";
    Matricula matricula{};
    std::string matricula_texto;
    int timeout_ms = 300;
    int max_tentativas = 3;
    int silencio_ms = 20;
    uint8_t endereco_didatico = 0x01;
};
Resultado<Configuracao> interpretar_configuracao(const std::string &conteudo);
Resultado<Configuracao> carregar_configuracao(const std::string &caminho);

#endif
