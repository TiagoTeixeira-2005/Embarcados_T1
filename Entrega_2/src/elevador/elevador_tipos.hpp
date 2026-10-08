#ifndef ELEVADOR_TIPOS_HPP
#define ELEVADOR_TIPOS_HPP

#include <cstdint>
constexpr uint8_t ENDERECO_CABINE_1 = 0x11;
constexpr uint8_t ENDERECO_CABINE_2 = 0x12;
constexpr uint8_t ENDERECO_CABINE_3 = 0x13;
constexpr uint8_t ENDERECO_PREDIO = 0x20;
constexpr int NUMERO_CABINES = 3;
constexpr uint16_t CAB_ANDAR_ATUAL = 0;
constexpr uint16_t CAB_NIVELADO = 1;
constexpr uint16_t CAB_PORTA_ESTADO = 2;
constexpr uint16_t CAB_PORTA_COMANDO = 3;
constexpr uint16_t CAB_CORRENTE_MA = 4;
constexpr uint16_t CAB_CARGA_KG = 5;
constexpr uint16_t CAB_PASSAGEIROS = 6;
constexpr uint16_t CAB_POSICAO_MM = 7;
constexpr uint16_t CAB_FALHA = 8;
constexpr uint16_t CABINE_TOTAL_REGISTRADORES = 9;
constexpr uint16_t PRED_CHAMADAS_NA_FILA = 0;
constexpr uint16_t PRED_CHAMADA_ORIGEM = 1;
constexpr uint16_t PRED_CHAMADA_DESTINO = 2;
constexpr uint16_t PRED_CHAMADA_ID = 3;
constexpr uint16_t PRED_CHAMADA_POP = 4;
constexpr uint16_t PRED_AMBIENTE_TEMP_C_X10 = 5;
constexpr uint16_t PRED_AMBIENTE_PRESS_HPA = 6;
constexpr uint16_t PRED_BARRAMENTO_MAX_MA = 7;
constexpr uint16_t PRED_WATCHDOG_AMBIENTE = 8;
constexpr uint16_t PRED_ATRIBUICAO_CHAMADA_ID = 9;
constexpr uint16_t PRED_ATRIBUICAO_CABINE = 10;
constexpr uint16_t PRED_CENARIO_ATIVO = 11;
constexpr uint16_t PRED_CENARIO_GERADAS = 12;
constexpr uint16_t PRED_CENARIO_ATENDIDAS = 13;
constexpr uint16_t PRED_ESPERA_MEDIA_S_X10 = 14;
constexpr uint16_t PRED_VIAGEM_MEDIA_S_X10 = 15;
constexpr uint16_t PREDIO_TOTAL_REGISTRADORES = 16;

enum class ComandoPorta : uint16_t { Nenhum = 0, Abrir = 1, Fechar = 2 };
inline uint8_t endereco_cabine(int cabine) {
    return (cabine >= 1 && cabine <= NUMERO_CABINES) ? static_cast<uint8_t>(0x10 + cabine) : 0;
}

inline bool endereco_e_cabine(uint8_t endereco) {
    return endereco >= ENDERECO_CABINE_1 && endereco <= ENDERECO_CABINE_3;
}
inline uint16_t total_registradores_mapa(uint8_t endereco) {
    if (endereco_e_cabine(endereco)) {
        return CABINE_TOTAL_REGISTRADORES;
    }
    if (endereco == ENDERECO_PREDIO) {
        return PREDIO_TOTAL_REGISTRADORES;
    }
    return 0;
}
inline bool registrador_gravavel(uint8_t endereco, uint16_t reg) {
    if (endereco_e_cabine(endereco)) {
        return reg == CAB_PORTA_COMANDO;
    }
    if (endereco == ENDERECO_PREDIO) {
        return reg == PRED_CHAMADA_POP || reg == PRED_AMBIENTE_TEMP_C_X10 ||
               reg == PRED_AMBIENTE_PRESS_HPA || reg == PRED_ATRIBUICAO_CHAMADA_ID ||
               reg == PRED_ATRIBUICAO_CABINE;
    }
    return false;
}
inline const char *nome_registrador(uint8_t endereco, uint16_t reg) {
    static const char *const CABINE[CABINE_TOTAL_REGISTRADORES] = {
        "andar_atual", "nivelado", "porta_estado", "porta_comando", "corrente_ma",
        "carga_kg", "passageiros", "posicao_mm", "falha"};
    static const char *const PREDIO[PREDIO_TOTAL_REGISTRADORES] = {
        "chamadas_na_fila", "chamada_origem", "chamada_destino", "chamada_id",
        "chamada_pop", "ambiente_temp_c_x10", "ambiente_press_hpa", "barramento_max_ma",
        "watchdog_ambiente", "atribuicao_chamada_id", "atribuicao_cabine", "cenario_ativo",
        "cenario_geradas", "cenario_atendidas", "espera_media_s_x10", "viagem_media_s_x10"};

    if (endereco_e_cabine(endereco) && reg < CABINE_TOTAL_REGISTRADORES) {
        return CABINE[reg];
    }
    if (endereco == ENDERECO_PREDIO && reg < PREDIO_TOTAL_REGISTRADORES) {
        return PREDIO[reg];
    }
    return "(fora do mapa)";
}

inline const char *nome_porta_estado(uint16_t estado) {
    switch (estado) {
        case 0: return "fechada";
        case 1: return "abrindo";
        case 2: return "aberta";
        case 3: return "fechando";
        case 4: return "obstruida";
        default: return "desconhecido";
    }
}

inline const char *nome_cenario(uint16_t cenario) {
    switch (cenario) {
        case 0: return "nenhum";
        case 1: return "up-peak";
        case 2: return "down-peak";
        case 3: return "interfloor";
        default: return "desconhecido";
    }
}
struct EstadoCabine {
    uint16_t andar_atual = 0;
    uint16_t nivelado = 0;
    uint16_t porta_estado = 0;
    uint16_t porta_comando = 0;
    uint16_t corrente_ma = 0;
    uint16_t carga_kg = 0;
    uint16_t passageiros = 0;
    int16_t posicao_mm = 0;
    uint16_t falha = 0;
};
struct EstadoPredio {
    uint16_t chamadas_na_fila = 0;
    uint16_t chamada_origem = 0;
    uint16_t chamada_destino = 0;
    uint16_t chamada_id = 0;
    uint16_t chamada_pop = 0;
    uint16_t ambiente_temp_c_x10 = 0;
    uint16_t ambiente_press_hpa = 0;
    uint16_t barramento_max_ma = 0;
    uint16_t watchdog_ambiente = 0;
    uint16_t atribuicao_chamada_id = 0;
    uint16_t atribuicao_cabine = 0;
    uint16_t cenario_ativo = 0;
    uint16_t cenario_geradas = 0;
    uint16_t cenario_atendidas = 0;
    uint16_t espera_media_s_x10 = 0;
    uint16_t viagem_media_s_x10 = 0;
};
struct ChamadaFila {
    uint16_t chamadas_na_fila = 0;
    uint16_t origem = 0;
    uint16_t destino = 0;
    uint16_t id = 0;

    bool vazia() const { return chamadas_na_fila == 0; }
};

#endif
