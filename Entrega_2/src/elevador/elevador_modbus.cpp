#include "elevador/elevador_modbus.hpp"

#include <string>

#include "common/bytes.hpp"
#include "modbus/crc16.hpp"

namespace {

constexpr uint8_t FUNCAO_LER = 0x03;
constexpr uint8_t FUNCAO_ESCREVER = 0x10;

constexpr uint16_t MAX_REGISTRADORES_LEITURA = 125;
constexpr size_t MAX_REGISTRADORES_ESCRITA = 123;

constexpr size_t OVERHEAD_RESPOSTA_LEITURA = 5;
constexpr size_t TAMANHO_RESPOSTA_ESCRITA = 8;

}

std::vector<uint8_t> montar_leitura(uint8_t endereco, uint16_t reg, uint16_t qtd,
                                    const Matricula &matricula) {
    std::vector<uint8_t> pacote;
    pacote.push_back(endereco);
    pacote.push_back(FUNCAO_LER);
    escrever_u16_le(pacote, reg);
    escrever_u16_le(pacote, qtd);
    anexar_matricula(pacote, matricula);
    anexar_crc16(pacote);
    return pacote;
}

std::vector<uint8_t> montar_escrita(uint8_t endereco, uint16_t reg,
                                    const std::vector<uint16_t> &valores,
                                    const Matricula &matricula) {
    std::vector<uint8_t> pacote;
    pacote.push_back(endereco);
    pacote.push_back(FUNCAO_ESCREVER);
    escrever_u16_le(pacote, reg);
    escrever_u16_le(pacote, static_cast<uint16_t>(valores.size()));
    pacote.push_back(static_cast<uint8_t>(2 * valores.size()));
    for (uint16_t valor : valores) {
        escrever_u16_le(pacote, valor);
    }
    anexar_matricula(pacote, matricula);
    anexar_crc16(pacote);
    return pacote;
}

ElevadorModbus::ElevadorModbus(RtuClient &cliente, const Matricula &matricula)
    : cliente_(cliente), matricula_(matricula) {}

Resultado<std::vector<uint16_t>> ElevadorModbus::le_registradores(uint8_t endereco, uint16_t reg,
                                                                  uint16_t qtd) {
    typedef Resultado<std::vector<uint16_t>> Res;
    if (qtd > MAX_REGISTRADORES_LEITURA) {
        return Res::com_falha(Status::falha(Erro::ArgumentoInvalido, "qtd maior que 125 registradores"));
    }

    const size_t esperado = OVERHEAD_RESPOSTA_LEITURA + 2u * qtd;
    const Resultado<std::vector<uint8_t>> r =
        cliente_.transacao(montar_leitura(endereco, reg, qtd, matricula_), esperado);
    if (!r.ok()) {
        return Res::com_falha(r.status);
    }
    const std::vector<uint8_t> &trama = r.valor;
    if (trama.size() != esperado) {
        return Res::com_falha(Status::falha(
            Erro::TamanhoInvalido, "recebidos " + std::to_string(trama.size()) +
                                       " bytes, esperados " + std::to_string(esperado)));
    }
    if (trama[2] != 2 * qtd) {
        return Res::com_falha(Status::falha(
            Erro::ByteCountInvalido, "byte_count=" + std::to_string(trama[2]) +
                                         ", esperado " + std::to_string(2 * qtd)));
    }

    std::vector<uint16_t> valores;
    valores.reserve(qtd);
    for (uint16_t i = 0; i < qtd; ++i) {
        valores.push_back(ler_u16_be(&trama[3 + 2u * i]));
    }
    return Res::com_valor(std::move(valores));
}

Status ElevadorModbus::escreve_registradores(uint8_t endereco, uint16_t reg,
                                             const std::vector<uint16_t> &valores) {
    if (valores.size() > MAX_REGISTRADORES_ESCRITA) {
        return Status::falha(Erro::ArgumentoInvalido, "mais de 123 registradores em uma escrita");
    }
    const uint16_t qtd = static_cast<uint16_t>(valores.size());

    const Resultado<std::vector<uint8_t>> r =
        cliente_.transacao(montar_escrita(endereco, reg, valores, matricula_),
                           TAMANHO_RESPOSTA_ESCRITA);
    if (!r.ok()) {
        return r.status;
    }

    const std::vector<uint8_t> &trama = r.valor;
    if (trama.size() != TAMANHO_RESPOSTA_ESCRITA) {
        return Status::falha(Erro::TamanhoInvalido,
                             "recebidos " + std::to_string(trama.size()) + " bytes, esperados 8");
    }

    const uint16_t reg_eco = ler_u16_be(&trama[2]);
    const uint16_t qtd_eco = ler_u16_be(&trama[4]);
    if (reg_eco != reg || qtd_eco != qtd) {
        return Status::falha(Erro::EcoDiferente,
                             "eco reg=" + std::to_string(reg_eco) + " qtd=" + std::to_string(qtd_eco) +
                                 ", enviado reg=" + std::to_string(reg) + " qtd=" + std::to_string(qtd));
    }
    return Status::sucesso();
}

Resultado<EstadoCabine> ElevadorModbus::le_estado_cabine(int cabine) {
    const uint8_t endereco = endereco_cabine(cabine);
    if (endereco == 0) {
        return Resultado<EstadoCabine>::com_falha(
            Status::falha(Erro::ArgumentoInvalido, "a cabine deve ser 1, 2 ou 3"));
    }

    const Resultado<std::vector<uint16_t>> r =
        le_registradores(endereco, 0, CABINE_TOTAL_REGISTRADORES);
    if (!r.ok()) {
        return Resultado<EstadoCabine>::com_falha(r.status);
    }
    if (r.valor.size() != CABINE_TOTAL_REGISTRADORES) {
        return Resultado<EstadoCabine>::com_falha(
            Status::falha(Erro::FormatoInvalido, "quantidade de registradores da cabine"));
    }

    const std::vector<uint16_t> &v = r.valor;
    EstadoCabine estado;
    estado.andar_atual = v[CAB_ANDAR_ATUAL];
    estado.nivelado = v[CAB_NIVELADO];
    estado.porta_estado = v[CAB_PORTA_ESTADO];
    estado.porta_comando = v[CAB_PORTA_COMANDO];
    estado.corrente_ma = v[CAB_CORRENTE_MA];
    estado.carga_kg = v[CAB_CARGA_KG];
    estado.passageiros = v[CAB_PASSAGEIROS];
    estado.posicao_mm = static_cast<int16_t>(v[CAB_POSICAO_MM]);
    estado.falha = v[CAB_FALHA];
    return Resultado<EstadoCabine>::com_valor(estado);
}

Resultado<EstadoPredio> ElevadorModbus::le_estado_predio() {
    const Resultado<std::vector<uint16_t>> r =
        le_registradores(ENDERECO_PREDIO, 0, PREDIO_TOTAL_REGISTRADORES);
    if (!r.ok()) {
        return Resultado<EstadoPredio>::com_falha(r.status);
    }
    if (r.valor.size() != PREDIO_TOTAL_REGISTRADORES) {
        return Resultado<EstadoPredio>::com_falha(
            Status::falha(Erro::FormatoInvalido, "quantidade de registradores do predio"));
    }

    const std::vector<uint16_t> &v = r.valor;
    EstadoPredio estado;
    estado.chamadas_na_fila = v[PRED_CHAMADAS_NA_FILA];
    estado.chamada_origem = v[PRED_CHAMADA_ORIGEM];
    estado.chamada_destino = v[PRED_CHAMADA_DESTINO];
    estado.chamada_id = v[PRED_CHAMADA_ID];
    estado.chamada_pop = v[PRED_CHAMADA_POP];
    estado.ambiente_temp_c_x10 = v[PRED_AMBIENTE_TEMP_C_X10];
    estado.ambiente_press_hpa = v[PRED_AMBIENTE_PRESS_HPA];
    estado.barramento_max_ma = v[PRED_BARRAMENTO_MAX_MA];
    estado.watchdog_ambiente = v[PRED_WATCHDOG_AMBIENTE];
    estado.atribuicao_chamada_id = v[PRED_ATRIBUICAO_CHAMADA_ID];
    estado.atribuicao_cabine = v[PRED_ATRIBUICAO_CABINE];
    estado.cenario_ativo = v[PRED_CENARIO_ATIVO];
    estado.cenario_geradas = v[PRED_CENARIO_GERADAS];
    estado.cenario_atendidas = v[PRED_CENARIO_ATENDIDAS];
    estado.espera_media_s_x10 = v[PRED_ESPERA_MEDIA_S_X10];
    estado.viagem_media_s_x10 = v[PRED_VIAGEM_MEDIA_S_X10];
    return Resultado<EstadoPredio>::com_valor(estado);
}

Status ElevadorModbus::comanda_porta(int cabine, ComandoPorta comando) {
    const uint8_t endereco = endereco_cabine(cabine);
    if (endereco == 0) {
        return Status::falha(Erro::ArgumentoInvalido, "a cabine deve ser 1, 2 ou 3");
    }
    return escreve_registradores(endereco, CAB_PORTA_COMANDO,
                                 {static_cast<uint16_t>(comando)});
}

Status ElevadorModbus::escreve_condicao_contorno(uint16_t temp_c_x10, uint16_t press_hpa) {
    return escreve_registradores(ENDERECO_PREDIO, PRED_AMBIENTE_TEMP_C_X10,
                                 {temp_c_x10, press_hpa});
}

Resultado<ChamadaFila> ElevadorModbus::le_chamada_da_fila() {
    const Resultado<std::vector<uint16_t>> r =
        le_registradores(ENDERECO_PREDIO, PRED_CHAMADAS_NA_FILA, 4);
    if (!r.ok()) {
        return Resultado<ChamadaFila>::com_falha(r.status);
    }
    if (r.valor.size() != 4) {
        return Resultado<ChamadaFila>::com_falha(
            Status::falha(Erro::FormatoInvalido, "quantidade de registradores da chamada"));
    }

    ChamadaFila chamada;
    chamada.chamadas_na_fila = r.valor[0];
    chamada.origem = r.valor[1];
    chamada.destino = r.valor[2];
    chamada.id = r.valor[3];
    return Resultado<ChamadaFila>::com_valor(chamada);
}

Status ElevadorModbus::atribui_chamada(uint16_t chamada_id, int cabine) {
    if (cabine < 1 || cabine > NUMERO_CABINES) {
        return Status::falha(Erro::ArgumentoInvalido, "a cabine atribuida deve ser 1, 2 ou 3");
    }
    return escreve_registradores(ENDERECO_PREDIO, PRED_ATRIBUICAO_CHAMADA_ID,
                                 {chamada_id, static_cast<uint16_t>(cabine)});
}

Status ElevadorModbus::remove_chamada_da_fila() {
    return escreve_registradores(ENDERECO_PREDIO, PRED_CHAMADA_POP, {1});
}
