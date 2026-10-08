#include "config/config.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "common/texto.hpp"

namespace {
bool converter_inteiro(const std::string &texto, long minimo, long maximo, long &saida) {
    if (texto.empty()) {
        return false;
    }
    char *fim = nullptr;
    errno = 0;
    const long valor = std::strtol(texto.c_str(), &fim, 0);
    if (errno != 0 || *fim != '\0') {
        return false;
    }
    if (valor < minimo || valor > maximo) {
        return false;
    }
    saida = valor;
    return true;
}

Resultado<Configuracao> erro_na_linha(int numero, const std::string &mensagem) {
    return Resultado<Configuracao>::com_falha(Status::falha(
        Erro::ArgumentoInvalido, "config linha " + std::to_string(numero) + ": " + mensagem));
}

} // namespace

Resultado<Configuracao> interpretar_configuracao(const std::string &conteudo) {
    Configuracao cfg;
    bool tem_matricula = false;

    std::istringstream fluxo(conteudo);
    std::string linha;
    int numero = 0;

    while (std::getline(fluxo, linha)) {
        ++numero;
        linha = aparar(linha);
        if (linha.empty() || linha[0] == '#' || linha[0] == ';' || linha[0] == '[') {
            continue;
        }

        const size_t igual = linha.find('=');
        if (igual == std::string::npos) {
            return erro_na_linha(numero, "esperado o formato chave=valor");
        }
        const std::string chave = aparar(linha.substr(0, igual));
        const std::string valor = aparar(linha.substr(igual + 1));
        long numerico = 0;

        if (chave == "uart_dispositivo") {
            if (valor.empty()) {
                return erro_na_linha(numero, "uart_dispositivo nao pode ser vazio");
            }
            cfg.uart_dispositivo = valor;
        } else if (chave == "matricula") {
            const Resultado<Matricula> m = matricula_de_texto(valor);
            if (!m.ok()) {
                return erro_na_linha(numero, m.status.descricao());
            }
            cfg.matricula = m.valor;
            cfg.matricula_texto = valor;
            tem_matricula = true;
        } else if (chave == "timeout_ms") {
            if (!converter_inteiro(valor, 200, 500, numerico)) {
                return erro_na_linha(numero, "timeout_ms deve estar entre 200 e 500");
            }
            cfg.timeout_ms = static_cast<int>(numerico);
        } else if (chave == "max_tentativas") {
            if (!converter_inteiro(valor, 1, 3, numerico)) {
                return erro_na_linha(numero, "max_tentativas deve estar entre 1 e 3");
            }
            cfg.max_tentativas = static_cast<int>(numerico);
        } else if (chave == "silencio_ms") {
            if (!converter_inteiro(valor, 5, 200, numerico)) {
                return erro_na_linha(numero, "silencio_ms deve estar entre 5 e 200");
            }
            cfg.silencio_ms = static_cast<int>(numerico);
        } else if (chave == "endereco_modbus_didatico") {
            if (!converter_inteiro(valor, 0, 255, numerico)) {
                return erro_na_linha(numero, "endereco_modbus_didatico deve estar entre 0 e 255");
            }
            cfg.endereco_didatico = static_cast<uint8_t>(numerico);
        }
    }

    if (!tem_matricula) {
        return Resultado<Configuracao>::com_falha(Status::falha(
            Erro::ArgumentoInvalido, "a chave 'matricula' (6 digitos) nao foi definida"));
    }
    return Resultado<Configuracao>::com_valor(cfg);
}

Resultado<Configuracao> carregar_configuracao(const std::string &caminho) {
    std::ifstream arquivo(caminho);
    if (!arquivo.is_open()) {
        return Resultado<Configuracao>::com_falha(Status::falha(
            Erro::ErroIO, "nao foi possivel abrir '" + caminho +
                              "' (copie config.ini.example para config.ini e edite)"));
    }
    std::ostringstream conteudo;
    conteudo << arquivo.rdbuf();
    return interpretar_configuracao(conteudo.str());
}
