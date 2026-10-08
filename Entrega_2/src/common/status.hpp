#ifndef STATUS_HPP
#define STATUS_HPP

#include <cstdint>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>

enum class Erro {
    Nenhum = 0,
    Timeout,            // nenhum byte (ou trama incompleta) dentro do prazo
    CrcInvalido,        // CRC da resposta nao confere
    EnderecoDiferente,  // endereco da resposta != endereco da requisicao
    FuncaoDiferente,    // funcao da resposta != funcao da requisicao
    ByteCountInvalido,  // byte_count != 2 * qtd (funcao 0x03)
    EcoDiferente,       // eco de reg/qtd != requisicao (funcao 0x10)
    TamanhoInvalido,    // quantidade de bytes diferente da esperada
    FormatoInvalido,    // campos internos inconsistentes (ex.: tamanho de string)
    Excecao,            // resposta MODBUS com MSB da funcao setado
    ErroIO,             // falha de sistema (open, read, write, termios...)
    Interrompido,       // SIGINT / pedido de encerramento durante a operacao
    ArgumentoInvalido   // parametro recusado localmente, antes de enviar
};

inline const char *nome_erro(Erro erro) {
    switch (erro) {
        case Erro::Nenhum:            return "sem erro";
        case Erro::Timeout:           return "timeout";
        case Erro::CrcInvalido:       return "CRC invalido";
        case Erro::EnderecoDiferente: return "endereco da resposta diferente do enviado";
        case Erro::FuncaoDiferente:   return "funcao da resposta diferente da enviada";
        case Erro::ByteCountInvalido: return "byte_count diferente de 2*qtd";
        case Erro::EcoDiferente:      return "eco de reg/qtd diferente da requisicao";
        case Erro::TamanhoInvalido:   return "tamanho da resposta invalido";
        case Erro::FormatoInvalido:   return "formato da resposta invalido";
        case Erro::Excecao:           return "resposta de excecao MODBUS";
        case Erro::ErroIO:            return "erro de E/S";
        case Erro::Interrompido:      return "operacao interrompida";
        case Erro::ArgumentoInvalido: return "argumento invalido";
    }
    return "erro desconhecido";
}
inline const char *descricao_excecao(uint8_t codigo) {
    switch (codigo) {
        case 0x01: return "funcao invalida";
        case 0x02: return "endereco invalido";
        case 0x03: return "valor invalido";
        default:   return "codigo de excecao desconhecido";
    }
}

struct Status {
    Erro erro = Erro::Nenhum;
    uint8_t codigo_excecao = 0;  
    int codigo_errno = 0;        
    std::string detalhe;         

    bool ok() const { return erro == Erro::Nenhum; }
    bool deve_repetir() const {
        return erro == Erro::Timeout || erro == Erro::CrcInvalido;
    }

    static Status sucesso() { return Status{}; }

    static Status falha(Erro e, std::string det = "") {
        Status s;
        s.erro = e;
        s.detalhe = std::move(det);
        return s;
    }

    static Status excecao(uint8_t codigo) {
        Status s;
        s.erro = Erro::Excecao;
        s.codigo_excecao = codigo;
        return s;
    }

    static Status io(int codigo_errno_sistema, std::string det = "") {
        Status s;
        s.erro = Erro::ErroIO;
        s.codigo_errno = codigo_errno_sistema;
        s.detalhe = std::move(det);
        return s;
    }
    std::string descricao() const {
        std::string texto = nome_erro(erro);
        if (erro == Erro::Excecao) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), " 0x%02X (%s)",
                          static_cast<unsigned>(codigo_excecao),
                          descricao_excecao(codigo_excecao));
            texto += buf;
        }
        if (erro == Erro::ErroIO && codigo_errno != 0) {
            texto += ": " + std::error_code(codigo_errno, std::generic_category()).message();
        }
        if (!detalhe.empty()) {
            texto += " - " + detalhe;
        }
        return texto;
    }
};
template <typename T>
struct Resultado {
    Status status;
    T valor{};

    bool ok() const { return status.ok(); }

    static Resultado<T> com_valor(T v) {
        Resultado<T> r;
        r.valor = std::move(v);
        return r;
    }

    static Resultado<T> com_falha(Status s) {
        Resultado<T> r;
        r.status = std::move(s);
        return r;
    }
};

#endif
