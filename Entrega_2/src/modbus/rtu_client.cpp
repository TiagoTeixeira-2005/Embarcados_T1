#include "modbus/rtu_client.hpp"

#include <string>
#include <utility>

#include "common/shutdown.hpp"
#include "modbus/crc16.hpp"

namespace {

constexpr size_t TAMANHO_MIN_RESPOSTA = 5;
constexpr size_t TAMANHO_MAX_SONDA = 256;
constexpr uint8_t BIT_EXCECAO = 0x80;

}

Status validar_resposta(const std::vector<uint8_t> &requisicao,
                        const std::vector<uint8_t> &resposta) {
    if (requisicao.size() < 2) {
        return Status::falha(Erro::ArgumentoInvalido, "requisicao sem endereco/funcao");
    }
    if (!verificar_crc16(resposta)) {
        return Status::falha(Erro::CrcInvalido);
    }
    const uint8_t endereco_tx = requisicao[0];
    const uint8_t endereco_rx = resposta[0];
    const bool eh_parte2 = (endereco_tx == 0x01 || endereco_tx == 0x00);
    const bool endereco_valido = (endereco_rx == endereco_tx) || (eh_parte2 && endereco_rx == 0x00);

    if (!endereco_valido) {
        return Status::falha(Erro::EnderecoDiferente);
    }

    if (resposta[1] == static_cast<uint8_t>(requisicao[1] | BIT_EXCECAO)) {
        if (resposta.size() != TAMANHO_MIN_RESPOSTA) {
            return Status::falha(Erro::TamanhoInvalido, "resposta de excecao com tamanho diferente de 5");
        }
        return Status::excecao(resposta[2]);
    }
    if (resposta[1] != requisicao[1]) {
        return Status::falha(Erro::FuncaoDiferente);
    }
    return Status::sucesso();
}

RtuClient::RtuClient(PortaUart &porta, const OpcoesRtu &opcoes)
    : porta_(porta), opcoes_(opcoes) {}

void RtuClient::definir_trace(TraceFn trace) {
    std::lock_guard<std::mutex> trava(mutex_);
    trace_ = std::move(trace);
}

Status RtuClient::ler_tamanho_conhecido(std::vector<uint8_t> &resposta, Instante prazo,
                                        size_t tamanho_total) {
    Status st = porta_.receber(resposta, 2, prazo);
    if (!st.ok()) {
        return st;
    }
    if (resposta[1] & BIT_EXCECAO) {
        return porta_.receber(resposta, TAMANHO_MIN_RESPOSTA - 2, prazo);
    }
    return porta_.receber(resposta, tamanho_total - 2, prazo);
}

Resultado<std::vector<uint8_t>> RtuClient::executar(const std::vector<uint8_t> &requisicao,
                                                    const LeitorTrama &ler) {
    typedef Resultado<std::vector<uint8_t>> Res;
    Status ultimo = Status::falha(Erro::Timeout);

    for (int tentativa = 1; tentativa <= opcoes_.max_tentativas; ++tentativa) {
        if (encerramento_solicitado()) {
            return Res::com_falha(Status::falha(Erro::Interrompido));
        }

        porta_.descartar_entrada();
        if (trace_) {
            trace_(DirecaoTrace::Enviado, requisicao, tentativa);
        }

        Status st = porta_.enviar(requisicao);
        if (!st.ok()) {
            return Res::com_falha(st);
        }

        std::vector<uint8_t> resposta;
        st = ler(resposta, daqui_a_ms(opcoes_.timeout_ms));
        if (!resposta.empty() && trace_) {
            trace_(DirecaoTrace::Recebido, resposta, tentativa);
        }

        if (st.ok()) {
            st = validar_resposta(requisicao, resposta);
        }
        if (st.ok()) {
            return Res::com_valor(std::move(resposta));
        }

        ultimo = st;
        if (!st.deve_repetir()) {
            return Res::com_falha(st);
        }
    }

    const std::string aviso = "falhou apos " + std::to_string(opcoes_.max_tentativas) + " tentativa(s)";
    ultimo.detalhe = ultimo.detalhe.empty() ? aviso : ultimo.detalhe + "; " + aviso;
    return Res::com_falha(ultimo);
}

Resultado<std::vector<uint8_t>> RtuClient::transacao(const std::vector<uint8_t> &requisicao,
                                                     size_t tamanho_resposta) {
    typedef Resultado<std::vector<uint8_t>> Res;
    if (tamanho_resposta < TAMANHO_MIN_RESPOSTA) {
        return Res::com_falha(Status::falha(Erro::ArgumentoInvalido,
                                            "tamanho de resposta esperado menor que 5 bytes"));
    }

    std::lock_guard<std::mutex> trava(mutex_);
    return executar(requisicao, [this, tamanho_resposta](std::vector<uint8_t> &resposta,
                                                         Instante prazo) {
        return ler_tamanho_conhecido(resposta, prazo, tamanho_resposta);
    });
}

Resultado<std::vector<uint8_t>> RtuClient::transacao_por_silencio(
    const std::vector<uint8_t> &requisicao) {
    std::lock_guard<std::mutex> trava(mutex_);
    return executar(requisicao, [this](std::vector<uint8_t> &resposta, Instante prazo) {
        return porta_.receber_ate_silencio(resposta, TAMANHO_MAX_SONDA, prazo, opcoes_.silencio_ms);
    });
}

Resultado<std::vector<uint8_t>> RtuClient::sondar(const std::vector<uint8_t> &requisicao) {
    typedef Resultado<std::vector<uint8_t>> Res;
    std::lock_guard<std::mutex> trava(mutex_);

    if (encerramento_solicitado()) {
        return Res::com_falha(Status::falha(Erro::Interrompido));
    }

    porta_.descartar_entrada();
    if (trace_) {
        trace_(DirecaoTrace::Enviado, requisicao, 1);
    }

    Status st = porta_.enviar(requisicao);
    if (!st.ok()) {
        return Res::com_falha(st);
    }

    std::vector<uint8_t> resposta;
    st = porta_.receber_ate_silencio(resposta, TAMANHO_MAX_SONDA,
                                     daqui_a_ms(opcoes_.timeout_ms), opcoes_.silencio_ms);
    if (!resposta.empty() && trace_) {
        trace_(DirecaoTrace::Recebido, resposta, 1);
    }
    if (!st.ok()) {
        return Res::com_falha(st);
    }
    return Res::com_valor(std::move(resposta));
}