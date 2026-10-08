#ifndef RTU_CLIENT_HPP
#define RTU_CLIENT_HPP

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "common/status.hpp"
#include "common/trace.hpp"
#include "uart/uart.hpp"

struct OpcoesRtu {
    int timeout_ms = 300;
    int max_tentativas = 3;
    int silencio_ms = 20;
};
Status validar_resposta(const std::vector<uint8_t> &requisicao,
                        const std::vector<uint8_t> &resposta);

class RtuClient {
public:
    RtuClient(PortaUart &porta, const OpcoesRtu &opcoes);

    void definir_trace(TraceFn trace);
    const OpcoesRtu &opcoes() const { return opcoes_; }
    Resultado<std::vector<uint8_t>> transacao(const std::vector<uint8_t> &requisicao,
                                              size_t tamanho_resposta);
    Resultado<std::vector<uint8_t>> transacao_por_silencio(
        const std::vector<uint8_t> &requisicao);
    Resultado<std::vector<uint8_t>> sondar(const std::vector<uint8_t> &requisicao);

private:
    using LeitorTrama = std::function<Status(std::vector<uint8_t> &, Instante)>;

    Resultado<std::vector<uint8_t>> executar(const std::vector<uint8_t> &requisicao,
                                             const LeitorTrama &ler);
    Status ler_tamanho_conhecido(std::vector<uint8_t> &resposta, Instante prazo,
                                 size_t tamanho_total);

    PortaUart &porta_;
    OpcoesRtu opcoes_;
    TraceFn trace_;
    std::mutex mutex_;
};

#endif
