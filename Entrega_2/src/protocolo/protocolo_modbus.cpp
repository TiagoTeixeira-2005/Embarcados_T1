#include "protocolo/protocolo_modbus.hpp"

#include <algorithm>

#include "common/bytes.hpp"
#include "modbus/crc16.hpp"

namespace protocolo_modbus {

namespace {

constexpr size_t TAMANHO_DADO_NUMERICO = 4;
template <typename Aceita>
bool localizar_dados(const std::vector<uint8_t> &payload, uint8_t subcodigo,
                     const Matricula &matricula, Aceita aceita, std::vector<uint8_t> &dados) {
    for (int tira_sub = 0; tira_sub <= 1; ++tira_sub) {
        for (int tira_mat = 0; tira_mat <= 1; ++tira_mat) {
            size_t inicio = 0;
            size_t fim = payload.size();

            if (tira_sub) {
                if (payload.empty() || payload[0] != subcodigo) {
                    continue;
                }
                inicio = 1;
            }
            if (tira_mat) {
                if (fim - inicio < matricula.size() ||
                    !std::equal(matricula.begin(), matricula.end(), payload.end() - matricula.size())) {
                    continue;
                }
                fim -= matricula.size();
            }

            const std::vector<uint8_t> candidato(payload.begin() + static_cast<long>(inicio),
                                                 payload.begin() + static_cast<long>(fim));
            if (aceita(candidato)) {
                dados = candidato;
                return true;
            }
        }
    }
    return false;
}

Status formato_nao_reconhecido(const std::vector<uint8_t> &payload, const char *esperado) {
    return Status::falha(Erro::FormatoInvalido,
                         "payload de " + std::to_string(payload.size()) +
                             " bytes nao reconhecido (esperado " + esperado + ")");
}
Resultado<std::vector<uint8_t>> transacionar(const Contexto &ctx, uint8_t funcao,
                                             uint8_t subcodigo, const std::vector<uint8_t> &dados) {
    typedef Resultado<std::vector<uint8_t>> Res;
    if (ctx.cliente == nullptr) {
        return Res::com_falha(Status::falha(Erro::ErroIO, "cliente MODBUS nao inicializado"));
    }

    Res r = ctx.cliente->transacao_por_silencio(montar_requisicao(ctx, funcao, subcodigo, dados));
    if (!r.ok()) {
        return r;
    }
    if (r.valor.size() < 4) {
        return Res::com_falha(Status::falha(Erro::TamanhoInvalido, "trama menor que 4 bytes"));
    }
    return Res::com_valor(std::vector<uint8_t>(r.valor.begin() + 2, r.valor.end() - 2));
}

Resultado<std::vector<uint8_t>> dado_de_4_bytes(const Contexto &ctx, uint8_t funcao,
                                                uint8_t subcodigo, const std::vector<uint8_t> &dados) {
    typedef Resultado<std::vector<uint8_t>> Res;
    const Res r = transacionar(ctx, funcao, subcodigo, dados);
    if (!r.ok()) {
        return r;
    }
    std::vector<uint8_t> dado;
    const bool achou = localizar_dados(
        r.valor, subcodigo, ctx.matricula,
        [](const std::vector<uint8_t> &c) { return c.size() == TAMANHO_DADO_NUMERICO; }, dado);
    if (!achou) {
        return Res::com_falha(formato_nao_reconhecido(r.valor, "4 bytes"));
    }
    return Res::com_valor(dado);
}

Resultado<std::string> dado_de_string(const Contexto &ctx, uint8_t funcao, uint8_t subcodigo,
                                      const std::vector<uint8_t> &dados) {
    typedef Resultado<std::string> Res;
    const Resultado<std::vector<uint8_t>> r = transacionar(ctx, funcao, subcodigo, dados);
    if (!r.ok()) {
        return Res::com_falha(r.status);
    }
    std::vector<uint8_t> dado;
    const bool achou = localizar_dados(
        r.valor, subcodigo, ctx.matricula,
        [](const std::vector<uint8_t> &c) { return !c.empty() && c[0] == c.size() - 1; }, dado);
    if (!achou) {
        return Res::com_falha(formato_nao_reconhecido(r.valor, "[len][string]"));
    }
    return Res::com_valor(std::string(dado.begin() + 1, dado.end()));
}

}

std::vector<uint8_t> montar_requisicao(const Contexto &ctx, uint8_t funcao, uint8_t subcodigo,
                                       const std::vector<uint8_t> &dados) {
    std::vector<uint8_t> pacote;
    pacote.push_back(ctx.endereco);
    pacote.push_back(funcao);
    pacote.push_back(subcodigo);
    pacote.insert(pacote.end(), dados.begin(), dados.end());
    anexar_matricula(pacote, ctx.matricula);
    anexar_crc16(pacote);
    return pacote;
}

Resultado<std::vector<uint8_t>> sondar(const Contexto &ctx, uint8_t funcao, uint8_t subcodigo,
                                       const std::vector<uint8_t> &dados) {
    if (ctx.cliente == nullptr) {
        return Resultado<std::vector<uint8_t>>::com_falha(
            Status::falha(Erro::ErroIO, "cliente MODBUS nao inicializado"));
    }
    return ctx.cliente->sondar(montar_requisicao(ctx, funcao, subcodigo, dados));
}

Resultado<int32_t> solicita_inteiro(const Contexto &ctx) {
    const Resultado<std::vector<uint8_t>> r =
        dado_de_4_bytes(ctx, FUNCAO_SOLICITA, SUB_SOLICITA_INTEIRO, {});
    if (!r.ok()) {
        return Resultado<int32_t>::com_falha(r.status);
    }
    return Resultado<int32_t>::com_valor(ler_i32_le(r.valor.data()));
}

Resultado<float> solicita_float(const Contexto &ctx) {
    const Resultado<std::vector<uint8_t>> r =
        dado_de_4_bytes(ctx, FUNCAO_SOLICITA, SUB_SOLICITA_FLOAT, {});
    if (!r.ok()) {
        return Resultado<float>::com_falha(r.status);
    }
    return Resultado<float>::com_valor(ler_float_le(r.valor.data()));
}

Resultado<std::string> solicita_string(const Contexto &ctx) {
    return dado_de_string(ctx, FUNCAO_SOLICITA, SUB_SOLICITA_STRING, {});
}

Resultado<int32_t> envia_inteiro(const Contexto &ctx, int32_t valor) {
    std::vector<uint8_t> dados;
    escrever_i32_le(dados, valor);
    const Resultado<std::vector<uint8_t>> r =
        dado_de_4_bytes(ctx, FUNCAO_ENVIA, SUB_ENVIA_INTEIRO, dados);
    if (!r.ok()) {
        return Resultado<int32_t>::com_falha(r.status);
    }
    return Resultado<int32_t>::com_valor(ler_i32_le(r.valor.data()));
}

Resultado<float> envia_float(const Contexto &ctx, float valor) {
    std::vector<uint8_t> dados;
    escrever_float_le(dados, valor);
    const Resultado<std::vector<uint8_t>> r =
        dado_de_4_bytes(ctx, FUNCAO_ENVIA, SUB_ENVIA_FLOAT, dados);
    if (!r.ok()) {
        return Resultado<float>::com_falha(r.status);
    }
    return Resultado<float>::com_valor(ler_float_le(r.valor.data()));
}

Resultado<std::string> envia_string(const Contexto &ctx, const std::string &texto) {
    if (texto.size() > TAMANHO_MAX_STRING_ENVIO) {
        return Resultado<std::string>::com_falha(Status::falha(
            Erro::ArgumentoInvalido,
            "string maior que " + std::to_string(TAMANHO_MAX_STRING_ENVIO) + " caracteres"));
    }
    std::vector<uint8_t> dados;
    dados.push_back(static_cast<uint8_t>(texto.size()));
    dados.insert(dados.end(), texto.begin(), texto.end());
    return dado_de_string(ctx, FUNCAO_ENVIA, SUB_ENVIA_STRING, dados);
}

}
