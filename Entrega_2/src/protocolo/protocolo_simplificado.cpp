#include "protocolo/protocolo_simplificado.hpp"

#include "common/bytes.hpp"

namespace protocolo_simplificado {

namespace {

constexpr size_t TAMANHO_DADO_NUMERICO = 4;
Status transacionar(const Contexto &ctx, const std::vector<uint8_t> &pacote,
                    bool resposta_em_string, std::vector<uint8_t> &resposta) {
    if (ctx.porta == nullptr || !ctx.porta->aberta()) {
        return Status::falha(Erro::ErroIO, "porta UART fechada");
    }

    ctx.porta->descartar_entrada();
    if (ctx.trace) {
        ctx.trace(DirecaoTrace::Enviado, pacote, 1);
    }

    Status st = ctx.porta->enviar(pacote);
    if (!st.ok()) {
        return st;
    }

    const Instante prazo = daqui_a_ms(ctx.timeout_ms);
    if (resposta_em_string) {
        st = ctx.porta->receber(resposta, 1, prazo);
        if (st.ok()) {
            const size_t n = resposta[0];
            st = ctx.porta->receber(resposta, n, prazo);
        }
    } else {
        st = ctx.porta->receber(resposta, TAMANHO_DADO_NUMERICO, prazo);
    }

    if (!resposta.empty() && ctx.trace) {
        ctx.trace(DirecaoTrace::Recebido, resposta, 1);
    }
    return st;
}

std::string extrair_string(const std::vector<uint8_t> &resposta) {
    return std::string(resposta.begin() + 1, resposta.end());
}

}

std::vector<uint8_t> montar_solicitacao(uint8_t comando, const Matricula &matricula) {
    std::vector<uint8_t> pacote;
    pacote.push_back(comando);
    anexar_matricula(pacote, matricula);
    return pacote;
}

std::vector<uint8_t> montar_envio_inteiro(int32_t valor, const Matricula &matricula) {
    std::vector<uint8_t> pacote;
    pacote.push_back(CMD_ENVIA_INTEIRO);
    escrever_i32_le(pacote, valor);
    anexar_matricula(pacote, matricula);
    return pacote;
}

std::vector<uint8_t> montar_envio_float(float valor, const Matricula &matricula) {
    std::vector<uint8_t> pacote;
    pacote.push_back(CMD_ENVIA_FLOAT);
    escrever_float_le(pacote, valor);
    anexar_matricula(pacote, matricula);
    return pacote;
}

std::vector<uint8_t> montar_envio_string(const std::string &texto, const Matricula &matricula) {
    std::vector<uint8_t> pacote;
    pacote.push_back(CMD_ENVIA_STRING);
    pacote.push_back(static_cast<uint8_t>(texto.size()));
    pacote.insert(pacote.end(), texto.begin(), texto.end());
    anexar_matricula(pacote, matricula);
    return pacote;
}

Resultado<int32_t> solicita_inteiro(const Contexto &ctx) {
    std::vector<uint8_t> resposta;
    const Status st = transacionar(ctx, montar_solicitacao(CMD_SOLICITA_INTEIRO, ctx.matricula),
                                   false, resposta);
    if (!st.ok()) {
        return Resultado<int32_t>::com_falha(st);
    }
    return Resultado<int32_t>::com_valor(ler_i32_le(resposta.data()));
}

Resultado<float> solicita_float(const Contexto &ctx) {
    std::vector<uint8_t> resposta;
    const Status st = transacionar(ctx, montar_solicitacao(CMD_SOLICITA_FLOAT, ctx.matricula),
                                   false, resposta);
    if (!st.ok()) {
        return Resultado<float>::com_falha(st);
    }
    return Resultado<float>::com_valor(ler_float_le(resposta.data()));
}

Resultado<std::string> solicita_string(const Contexto &ctx) {
    std::vector<uint8_t> resposta;
    const Status st = transacionar(ctx, montar_solicitacao(CMD_SOLICITA_STRING, ctx.matricula),
                                   true, resposta);
    if (!st.ok()) {
        return Resultado<std::string>::com_falha(st);
    }
    return Resultado<std::string>::com_valor(extrair_string(resposta));
}

Resultado<int32_t> envia_inteiro(const Contexto &ctx, int32_t valor) {
    std::vector<uint8_t> resposta;
    const Status st = transacionar(ctx, montar_envio_inteiro(valor, ctx.matricula), false, resposta);
    if (!st.ok()) {
        return Resultado<int32_t>::com_falha(st);
    }
    return Resultado<int32_t>::com_valor(ler_i32_le(resposta.data()));
}

Resultado<float> envia_float(const Contexto &ctx, float valor) {
    std::vector<uint8_t> resposta;
    const Status st = transacionar(ctx, montar_envio_float(valor, ctx.matricula), false, resposta);
    if (!st.ok()) {
        return Resultado<float>::com_falha(st);
    }
    return Resultado<float>::com_valor(ler_float_le(resposta.data()));
}

Resultado<std::string> envia_string(const Contexto &ctx, const std::string &texto) {
    if (texto.size() > TAMANHO_MAX_STRING_ENVIO) {
        return Resultado<std::string>::com_falha(Status::falha(
            Erro::ArgumentoInvalido,
            "string maior que " + std::to_string(TAMANHO_MAX_STRING_ENVIO) + " caracteres"));
    }
    std::vector<uint8_t> resposta;
    const Status st = transacionar(ctx, montar_envio_string(texto, ctx.matricula), true, resposta);
    if (!st.ok()) {
        return Resultado<std::string>::com_falha(st);
    }
    return Resultado<std::string>::com_valor(extrair_string(resposta));
}

}
