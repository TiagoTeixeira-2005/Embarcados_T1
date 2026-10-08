#include "cli/cli.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "common/bytes.hpp"
#include "common/hexdump.hpp"
#include "common/matricula.hpp"
#include "common/shutdown.hpp"
#include "common/status.hpp"
#include "common/tempo.hpp"
#include "common/texto.hpp"
#include "common/trace.hpp"
#include "config/config.hpp"
#include "elevador/elevador_modbus.hpp"
#include "elevador/elevador_tipos.hpp"
#include "modbus/rtu_client.hpp"
#include "protocolo/protocolo_modbus.hpp"
#include "protocolo/protocolo_simplificado.hpp"
#include "uart/uart.hpp"

namespace {

constexpr int PERIODO_LEITURA_CONTINUA_MS = 1000;
constexpr const char *PREFIXO_RESPOSTA_STRING = "Resposta da UART: ";

typedef std::vector<uint8_t> Bytes;

struct Sessao {
    const Configuracao &config;
    PortaUart &porta;
    RtuClient &cliente;
    ElevadorModbus &elevador;
};

bool ler_linha(const char *prompt, std::string &linha) {
    if (encerramento_solicitado()) {
        return false;
    }
    std::fputs(prompt, stdout);
    std::fflush(stdout);
    if (!std::getline(std::cin, linha)) {
        std::cin.clear();
        return false;
    }
    return true;
}

bool ler_inteiro(const char *prompt, long long minimo, long long maximo, long long &valor,
                 int base = 10) {
    std::string linha;
    if (!ler_linha(prompt, linha)) {
        return false;
    }
    const std::string texto = aparar(linha);
    if (texto.empty()) {
        std::puts("[CLI] Entrada vazia");
        return false;
    }
    char *fim = nullptr;
    errno = 0;
    const long long lido = std::strtoll(texto.c_str(), &fim, base);
    if (errno != 0 || *fim != '\0') {
        std::printf("[CLI] Entrada invalida: '%s'\n", texto.c_str());
        return false;
    }
    if (lido < minimo || lido > maximo) {
        std::printf("[CLI] Valor fora da faixa (%lld a %lld)\n", minimo, maximo);
        return false;
    }
    valor = lido;
    return true;
}

bool ler_real(const char *prompt, double minimo, double maximo, double &valor) {
    std::string linha;
    if (!ler_linha(prompt, linha)) {
        return false;
    }
    const std::string texto = aparar(linha);
    if (texto.empty()) {
        std::puts("[CLI] Entrada vazia");
        return false;
    }
    char *fim = nullptr;
    errno = 0;
    const double lido = std::strtod(texto.c_str(), &fim);
    if (errno != 0 || *fim != '\0' || std::isnan(lido)) {
        std::printf("[CLI] Entrada invalida: '%s'\n", texto.c_str());
        return false;
    }
    if (lido < minimo || lido > maximo) {
        std::printf("[CLI] Valor fora da faixa (%g a %g)\n", minimo, maximo);
        return false;
    }
    valor = lido;
    return true;
}

void imprimir_erro(const Status &st) {
    if (st.erro == Erro::Excecao) {
        std::printf("[EXCECAO] %s\n", st.descricao().c_str());
    } else {
        std::printf("[ERRO] %s\n", st.descricao().c_str());
    }
}

TraceFn criar_trace(int max_tentativas) {
    return [max_tentativas](DirecaoTrace direcao, const Bytes &bytes, int tentativa) {
        std::printf("  [%s #%d/%d] (%zu bytes) %s\n",
                    direcao == DirecaoTrace::Enviado ? "TX" : "RX", tentativa, max_tentativas,
                    bytes.size(), bytes_para_hex(bytes).c_str());
    };
}

protocolo_simplificado::Contexto contexto_simples(const Sessao &s) {
    protocolo_simplificado::Contexto ctx;
    ctx.porta = &s.porta;
    ctx.matricula = s.config.matricula;
    ctx.timeout_ms = s.config.timeout_ms;
    ctx.trace = criar_trace(1);
    return ctx;
}

void mostrar_conferencia(bool confere) {
    std::printf("  Conferencia: %s\n", confere ? "confere" : "DIVERGE do esperado");
}

int32_t multiplicar_com_wrap(int32_t valor, uint8_t digito) {
    return static_cast<int32_t>(static_cast<uint32_t>(valor) * digito);
}

bool ler_texto_para_envio(std::string &texto) {
    std::string linha;
    if (!ler_linha("Texto (ate 237 caracteres): ", linha)) {
        return false;
    }
    if (linha.size() > protocolo_simplificado::TAMANHO_MAX_STRING_ENVIO) {
        std::puts("[CLI] Texto maior que 237 caracteres");
        return false;
    }
    texto = linha;
    return true;
}

void simples_solicita_inteiro(const Sessao &s) {
    std::puts("\n[PARTE 1] Solicita inteiro (0xA1)");
    const auto r = protocolo_simplificado::solicita_inteiro(contexto_simples(s));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  Inteiro recebido: %d\n", static_cast<int>(r.valor));
}

void simples_solicita_float(const Sessao &s) {
    std::puts("\n[PARTE 1] Solicita float (0xA2)");
    const auto r = protocolo_simplificado::solicita_float(contexto_simples(s));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  Float recebido: %.6g\n", static_cast<double>(r.valor));
}

void simples_solicita_string(const Sessao &s) {
    std::puts("\n[PARTE 1] Solicita string (0xA3)");
    const auto r = protocolo_simplificado::solicita_string(contexto_simples(s));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  String recebida (%zu caracteres): \"%s\"\n", r.valor.size(), r.valor.c_str());
}

void simples_envia_inteiro(const Sessao &s) {
    std::puts("\n[PARTE 1] Envia inteiro (0xB1)");
    long long valor = 0;
    if (!ler_inteiro("Inteiro [-2147483648 a 2147483647]: ", INT32_MIN, INT32_MAX, valor)) return;
    const auto r = protocolo_simplificado::envia_inteiro(contexto_simples(s), static_cast<int32_t>(valor));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    const int32_t esperado = multiplicar_com_wrap(static_cast<int32_t>(valor), ultimo_digito(s.config.matricula));
    std::printf("  Resposta: %d (esperado: enviado x ultimo digito %d = %d)\n",
                static_cast<int>(r.valor), ultimo_digito(s.config.matricula), static_cast<int>(esperado));
    mostrar_conferencia(r.valor == esperado);
}

void simples_envia_float(const Sessao &s) {
    std::puts("\n[PARTE 1] Envia float (0xB2)");
    double valor = 0;
    if (!ler_real("Float: ", -1e30, 1e30, valor)) return;
    const float enviado = static_cast<float>(valor);
    const auto r = protocolo_simplificado::envia_float(contexto_simples(s), enviado);
    if (!r.ok()) { imprimir_erro(r.status); return; }
    const float esperado = enviado * static_cast<float>(ultimo_digito(s.config.matricula));
    std::printf("  Resposta: %.6g (esperado: enviado x ultimo digito %d = %.6g)\n",
                static_cast<double>(r.valor), ultimo_digito(s.config.matricula), static_cast<double>(esperado));
    mostrar_conferencia(r.valor == esperado);
}

void simples_envia_string(const Sessao &s) {
    std::puts("\n[PARTE 1] Envia string (0xB3)");
    std::string texto;
    if (!ler_texto_para_envio(texto)) return;
    const auto r = protocolo_simplificado::envia_string(contexto_simples(s), texto);
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  String recebida (%zu caracteres): \"%s\"\n", r.valor.size(), r.valor.c_str());
    mostrar_conferencia(r.valor == std::string(PREFIXO_RESPOSTA_STRING) + texto);
}

void menu_simplificado(const Sessao &s) {
    while (true) {
        std::puts("\n=== PARTE 1 - Protocolo simplificado (sem MODBUS, sem CRC) ===");
        std::puts("  1) Solicita inteiro (0xA1)");
        std::puts("  2) Solicita float   (0xA2)");
        std::puts("  3) Solicita string  (0xA3)");
        std::puts("  4) Envia inteiro    (0xB1)");
        std::puts("  5) Envia float      (0xB2)");
        std::puts("  6) Envia string     (0xB3)");
        std::puts("  0) Voltar");
        std::string linha;
        if (!ler_linha("parte1> ", linha)) return;
        const std::string op = aparar(linha);
        if (op == "1") simples_solicita_inteiro(s);
        else if (op == "2") simples_solicita_float(s);
        else if (op == "3") simples_solicita_string(s);
        else if (op == "4") simples_envia_inteiro(s);
        else if (op == "5") simples_envia_float(s);
        else if (op == "6") simples_envia_string(s);
        else if (op == "0") return;
        else if (!op.empty()) std::puts("[CLI] Opcao invalida");
    }
}

protocolo_modbus::Contexto contexto_modbus(const Sessao &s) {
    protocolo_modbus::Contexto ctx;
    ctx.cliente = &s.cliente;
    ctx.matricula = s.config.matricula;
    ctx.endereco = s.config.endereco_didatico;
    return ctx;
}

void modbus_solicita_inteiro(const Sessao &s) {
    std::puts("\n[PARTE 2] MODBUS: solicita inteiro (0x23 0xA1)");
    const auto r = protocolo_modbus::solicita_inteiro(contexto_modbus(s));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  Inteiro recebido: %d\n", static_cast<int>(r.valor));
}

void modbus_solicita_float(const Sessao &s) {
    std::puts("\n[PARTE 2] MODBUS: solicita float (0x23 0xA2)");
    const auto r = protocolo_modbus::solicita_float(contexto_modbus(s));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  Float recebido: %.6g\n", static_cast<double>(r.valor));
}

void modbus_solicita_string(const Sessao &s) {
    std::puts("\n[PARTE 2] MODBUS: solicita string (0x23 0xA3)");
    const auto r = protocolo_modbus::solicita_string(contexto_modbus(s));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  String recebida (%zu caracteres): \"%s\"\n", r.valor.size(), r.valor.c_str());
}

void modbus_envia_inteiro(const Sessao &s) {
    std::puts("\n[PARTE 2] MODBUS: envia inteiro (0x16 0xB1)");
    long long valor = 0;
    if (!ler_inteiro("Inteiro [-2147483648 a 2147483647]: ", INT32_MIN, INT32_MAX, valor)) return;
    const auto r = protocolo_modbus::envia_inteiro(contexto_modbus(s), static_cast<int32_t>(valor));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  Inteiro devolvido: %d\n", static_cast<int>(r.valor));
}

void modbus_envia_float(const Sessao &s) {
    std::puts("\n[PARTE 2] MODBUS: envia float (0x16 0xB2)");
    double valor = 0;
    if (!ler_real("Float: ", -1e30, 1e30, valor)) return;
    const auto r = protocolo_modbus::envia_float(contexto_modbus(s), static_cast<float>(valor));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  Float devolvido: %.6g\n", static_cast<double>(r.valor));
}

void modbus_envia_string(const Sessao &s) {
    std::puts("\n[PARTE 2] MODBUS: envia string (0x16 0xB3)");
    std::string texto;
    if (!ler_texto_para_envio(texto)) return;
    const auto r = protocolo_modbus::envia_string(contexto_modbus(s), texto);
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  String devolvida (%zu caracteres): \"%s\"\n", r.valor.size(), r.valor.c_str());
}

void modbus_sonda(const Sessao &s) {
    std::puts("\n[PARTE 2] Sonda: resposta bruta, sem validacao");
    std::puts("  1) 0x23 0xA1   2) 0x23 0xA2   3) 0x23 0xA3");
    std::puts("  4) 0x16 0xB1   5) 0x16 0xB2   6) 0x16 0xB3");
    long long n = 0;
    if (!ler_inteiro("Comando [1-6]: ", 1, 6, n)) return;

    static const uint8_t SUBCODIGOS[6] = {
        protocolo_modbus::SUB_SOLICITA_INTEIRO, protocolo_modbus::SUB_SOLICITA_FLOAT,
        protocolo_modbus::SUB_SOLICITA_STRING, protocolo_modbus::SUB_ENVIA_INTEIRO,
        protocolo_modbus::SUB_ENVIA_FLOAT, protocolo_modbus::SUB_ENVIA_STRING};
    const uint8_t sub = SUBCODIGOS[n - 1];
    const uint8_t funcao = n <= 3 ? protocolo_modbus::FUNCAO_SOLICITA : protocolo_modbus::FUNCAO_ENVIA;

    Bytes dados;
    if (n == 4) {
        long long v = 0;
        if (!ler_inteiro("Inteiro: ", INT32_MIN, INT32_MAX, v)) return;
        escrever_i32_le(dados, static_cast<int32_t>(v));
    } else if (n == 5) {
        double v = 0;
        if (!ler_real("Float: ", -1e30, 1e30, v)) return;
        escrever_float_le(dados, static_cast<float>(v));
    } else if (n == 6) {
        std::string texto;
        if (!ler_texto_para_envio(texto)) return;
        dados.push_back(static_cast<uint8_t>(texto.size()));
        dados.insert(dados.end(), texto.begin(), texto.end());
    }

    const auto r = protocolo_modbus::sondar(contexto_modbus(s), funcao, sub, dados);
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  %zu byte(s) recebidos (sem validacao de CRC, endereco ou funcao)\n", r.valor.size());
}

void menu_modbus_didatico(const Sessao &s) {
    while (true) {
        std::printf("\n=== PARTE 2 - MODBUS modificado (endereco 0x%02X, matricula %s) ===\n",
                    static_cast<unsigned>(s.config.endereco_didatico), s.config.matricula_texto.c_str());
        std::puts("  1) Solicita inteiro (0x23 0xA1)");
        std::puts("  2) Solicita float   (0x23 0xA2)");
        std::puts("  3) Solicita string  (0x23 0xA3)");
        std::puts("  4) Envia inteiro    (0x16 0xB1)");
        std::puts("  5) Envia float      (0x16 0xB2)");
        std::puts("  6) Envia string     (0x16 0xB3)");
        std::puts("  7) Sonda: resposta bruta de um comando");
        std::puts("  0) Voltar");
        std::string linha;
        if (!ler_linha("parte2> ", linha)) return;
        const std::string op = aparar(linha);
        if (op == "1") modbus_solicita_inteiro(s);
        else if (op == "2") modbus_solicita_float(s);
        else if (op == "3") modbus_solicita_string(s);
        else if (op == "4") modbus_envia_inteiro(s);
        else if (op == "5") modbus_envia_float(s);
        else if (op == "6") modbus_envia_string(s);
        else if (op == "7") modbus_sonda(s);
        else if (op == "0") return;
        else if (!op.empty()) std::puts("[CLI] Opcao invalida");
    }
}

void imprimir_estado_cabine(int cabine, const EstadoCabine &e) {
    std::printf("  ---- Cabine %d (0x%02X) ----\n", cabine, static_cast<unsigned>(endereco_cabine(cabine)));
    std::printf("  [0] andar_atual   : %u\n", static_cast<unsigned>(e.andar_atual));
    std::printf("  [1] nivelado      : %u (%s)\n", static_cast<unsigned>(e.nivelado),
                e.nivelado ? "nivelado" : "entre andares");
    std::printf("  [2] porta_estado  : %u (%s)\n", static_cast<unsigned>(e.porta_estado),
                nome_porta_estado(e.porta_estado));
    std::printf("  [3] porta_comando : %u\n", static_cast<unsigned>(e.porta_comando));
    std::printf("  [4] corrente_ma   : %u mA\n", static_cast<unsigned>(e.corrente_ma));
    std::printf("  [5] carga_kg      : %u kg\n", static_cast<unsigned>(e.carga_kg));
    std::printf("  [6] passageiros   : %u\n", static_cast<unsigned>(e.passageiros));
    std::printf("  [7] posicao_mm    : %d mm (int16 com sinal)\n", static_cast<int>(e.posicao_mm));
    std::printf("  [8] falha         : 0x%04X\n", static_cast<unsigned>(e.falha));
}

void imprimir_estado_predio(const EstadoPredio &e) {
    std::printf("  ---- Controlador do Predio (0x%02X) ----\n", static_cast<unsigned>(ENDERECO_PREDIO));
    std::printf("  [ 0] chamadas_na_fila      : %u\n", static_cast<unsigned>(e.chamadas_na_fila));
    std::printf("  [ 1] chamada_origem        : %u\n", static_cast<unsigned>(e.chamada_origem));
    std::printf("  [ 2] chamada_destino       : %u\n", static_cast<unsigned>(e.chamada_destino));
    std::printf("  [ 3] chamada_id            : %u\n", static_cast<unsigned>(e.chamada_id));
    std::printf("  [ 4] chamada_pop           : %u\n", static_cast<unsigned>(e.chamada_pop));
    std::printf("  [ 5] ambiente_temp_c_x10   : %u (%.1f C)\n", static_cast<unsigned>(e.ambiente_temp_c_x10),
                e.ambiente_temp_c_x10 / 10.0);
    std::printf("  [ 6] ambiente_press_hpa    : %u hPa\n", static_cast<unsigned>(e.ambiente_press_hpa));
    std::printf("  [ 7] barramento_max_ma     : %u mA\n", static_cast<unsigned>(e.barramento_max_ma));
    std::printf("  [ 8] watchdog_ambiente     : %u (%s)\n", static_cast<unsigned>(e.watchdog_ambiente),
                e.watchdog_ambiente ? "EXPIRADO, piso ativo" : "condicao de contorno valida");
    std::printf("  [ 9] atribuicao_chamada_id : %u\n", static_cast<unsigned>(e.atribuicao_chamada_id));
    std::printf("  [10] atribuicao_cabine     : %u\n", static_cast<unsigned>(e.atribuicao_cabine));
    std::printf("  [11] cenario_ativo         : %u (%s)\n", static_cast<unsigned>(e.cenario_ativo),
                nome_cenario(e.cenario_ativo));
    std::printf("  [12] cenario_geradas       : %u\n", static_cast<unsigned>(e.cenario_geradas));
    std::printf("  [13] cenario_atendidas     : %u\n", static_cast<unsigned>(e.cenario_atendidas));
    std::printf("  [14] espera_media_s_x10    : %u (%.1f s)\n", static_cast<unsigned>(e.espera_media_s_x10),
                e.espera_media_s_x10 / 10.0);
    std::printf("  [15] viagem_media_s_x10    : %u (%.1f s)\n", static_cast<unsigned>(e.viagem_media_s_x10),
                e.viagem_media_s_x10 / 10.0);
}

void elevador_le_estado_cabine(const Sessao &s) {
    std::puts("\n[PARTE 3] le_estado_cabine(): 0x03, registradores 0 a 8");
    long long cabine = 0;
    if (!ler_inteiro("Cabine [1-3]: ", 1, 3, cabine)) return;
    const auto r = s.elevador.le_estado_cabine(static_cast<int>(cabine));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    imprimir_estado_cabine(static_cast<int>(cabine), r.valor);
}

void elevador_le_estado_predio(const Sessao &s) {
    std::puts("\n[PARTE 3] le_estado_predio(): 0x03, registradores 0 a 15");
    const auto r = s.elevador.le_estado_predio();
    if (!r.ok()) { imprimir_erro(r.status); return; }
    imprimir_estado_predio(r.valor);
}

void elevador_comanda_porta(const Sessao &s) {
    std::puts("\n[PARTE 3] comanda_porta(): 0x10, registrador 3 da cabine");
    long long cabine = 0, comando = 0;
    if (!ler_inteiro("Cabine [1-3]: ", 1, 3, cabine)) return;
    if (!ler_inteiro("Comando [0=nenhum 1=abrir 2=fechar]: ", 0, 2, comando)) return;
    const Status st = s.elevador.comanda_porta(static_cast<int>(cabine), static_cast<ComandoPorta>(comando));
    if (!st.ok()) { imprimir_erro(st); return; }
    std::printf("  Comando de porta %lld enviado a cabine %lld (eco confere)\n", comando, cabine);
}

void elevador_escreve_condicao_contorno(const Sessao &s) {
    std::puts("\n[PARTE 3] escreve_condicao_contorno(): 0x10, registradores 5 e 6 do predio");
    double temperatura = 0;
    long long pressao = 0;
    if (!ler_real("Temperatura em C (ex.: 25.3): ", 0.0, 6553.5, temperatura)) return;
    if (!ler_inteiro("Pressao em hPa [0-65535]: ", 0, 65535, pressao)) return;
    const uint16_t temp_x10 = static_cast<uint16_t>(std::lround(temperatura * 10.0));
    const Status st = s.elevador.escreve_condicao_contorno(temp_x10, static_cast<uint16_t>(pressao));
    if (!st.ok()) { imprimir_erro(st); return; }
    std::printf("  Escritos: temp_c_x10=%u (%.1f C), press_hpa=%lld (eco confere)\n",
                static_cast<unsigned>(temp_x10), temp_x10 / 10.0, pressao);
}

void elevador_le_chamada(const Sessao &s) {
    std::puts("\n[PARTE 3] le_chamada_da_fila(): 0x03, registradores 0 a 3 do predio");
    const auto r = s.elevador.le_chamada_da_fila();
    if (!r.ok()) { imprimir_erro(r.status); return; }
    std::printf("  chamadas_na_fila: %u\n", static_cast<unsigned>(r.valor.chamadas_na_fila));
    if (r.valor.vazia()) {
        std::puts("  Fila vazia");
        return;
    }
    std::printf("  Cabeca da fila: id=%u, origem=%u, destino=%u\n", static_cast<unsigned>(r.valor.id),
                static_cast<unsigned>(r.valor.origem), static_cast<unsigned>(r.valor.destino));
}

void elevador_atribui_chamada(const Sessao &s) {
    std::puts("\n[PARTE 3] atribui_chamada(): 0x10, registradores 9 e 10 do predio");
    long long id = 0, cabine = 0;
    if (!ler_inteiro("Id da chamada [0-65535]: ", 0, 65535, id)) return;
    if (!ler_inteiro("Cabine [1-3]: ", 1, 3, cabine)) return;
    const Status st = s.elevador.atribui_chamada(static_cast<uint16_t>(id), static_cast<int>(cabine));
    if (!st.ok()) { imprimir_erro(st); return; }
    std::printf("  Chamada %lld atribuida a cabine %lld (eco confere)\n", id, cabine);
}

void elevador_remove_chamada(const Sessao &s) {
    std::puts("\n[PARTE 3] remove_chamada_da_fila(): 0x10, registrador 4 = 1 do predio");
    const Status st = s.elevador.remove_chamada_da_fila();
    if (!st.ok()) { imprimir_erro(st); return; }
    std::puts("  Chamada da cabeca da fila removida (eco confere)");
}

void elevador_leitura_bruta(const Sessao &s) {
    std::puts("\n[PARTE 3] Leitura bruta (0x03), sem validar o mapa");
    long long endereco = 0, reg = 0, qtd = 0;
    if (!ler_inteiro("Endereco em hexadecimal (ex.: 11 ou 20): ", 0, 255, endereco, 16)) return;
    if (!ler_inteiro("Registrador inicial [0-65535]: ", 0, 65535, reg)) return;
    if (!ler_inteiro("Quantidade [0-125]: ", 0, 125, qtd)) return;

    const auto r = s.elevador.le_registradores(static_cast<uint8_t>(endereco), static_cast<uint16_t>(reg),
                                               static_cast<uint16_t>(qtd));
    if (!r.ok()) { imprimir_erro(r.status); return; }
    for (size_t i = 0; i < r.valor.size(); ++i) {
        const uint16_t numero = static_cast<uint16_t>(reg + i);
        const uint8_t addr = static_cast<uint8_t>(endereco);
        std::printf("  reg %-3u %-22s = %u", static_cast<unsigned>(numero),
                    nome_registrador(addr, numero), static_cast<unsigned>(r.valor[i]));
        if (endereco_e_cabine(addr) && numero == CAB_POSICAO_MM) {
            std::printf(" (int16: %d)", static_cast<int>(static_cast<int16_t>(r.valor[i])));
        }
        std::puts("");
    }
}

void elevador_escrita_bruta(const Sessao &s) {
    std::puts("\n[PARTE 3] Escrita bruta (0x10), sem validar o mapa");
    long long endereco = 0, reg = 0;
    if (!ler_inteiro("Endereco em hexadecimal (ex.: 11 ou 20): ", 0, 255, endereco, 16)) return;
    if (!ler_inteiro("Registrador inicial [0-65535]: ", 0, 65535, reg)) return;

    std::string linha;
    if (!ler_linha("Valores [0-65535] separados por espaco (vazio = qtd 0): ", linha)) return;
    std::vector<uint16_t> valores;
    std::istringstream fluxo(linha);
    std::string token;
    while (fluxo >> token) {
        char *fim = nullptr;
        errno = 0;
        const long v = std::strtol(token.c_str(), &fim, 10);
        if (errno != 0 || *fim != '\0' || v < 0 || v > 65535) {
            std::printf("[CLI] Valor invalido: '%s'\n", token.c_str());
            return;
        }
        valores.push_back(static_cast<uint16_t>(v));
    }
    if (valores.size() > 123) {
        std::puts("[CLI] No maximo 123 valores por escrita");
        return;
    }

    const uint8_t addr = static_cast<uint8_t>(endereco);
    for (size_t i = 0; i < valores.size(); ++i) {
        const uint16_t numero = static_cast<uint16_t>(reg + i);
        if (!registrador_gravavel(addr, numero)) {
            std::printf("  [AVISO] reg %u (%s) nao e gravavel pelo mapa; enviando mesmo assim\n",
                        static_cast<unsigned>(numero), nome_registrador(addr, numero));
        }
    }

    const Status st = s.elevador.escreve_registradores(addr, static_cast<uint16_t>(reg), valores);
    if (!st.ok()) { imprimir_erro(st); return; }
    std::printf("  Escrita aceita: reg=%lld qtd=%zu (eco confere)\n", reg, valores.size());
}

enum class Espera { Prazo, Enter, Interrompido };

Espera esperar_prazo_ou_enter(Instante prazo) {
    while (true) {
        if (encerramento_solicitado()) {
            return Espera::Interrompido;
        }
        struct pollfd p;
        p.fd = STDIN_FILENO;
        p.events = POLLIN;
        p.revents = 0;
        const int r = ::poll(&p, 1, milissegundos_ate(prazo));
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Espera::Interrompido;
        }
        if (r == 0) {
            return Espera::Prazo;
        }
        char descarte[64];
        const ssize_t lidos = ::read(STDIN_FILENO, descarte, sizeof(descarte));
        if (lidos < 0 && errno == EINTR) {
            continue;
        }
        return Espera::Enter;
    }
}

void elevador_leitura_continua(const Sessao &s) {
    std::puts("\n[PARTE 3] Leitura continua (0x03), a cada 1 s");
    std::puts("  1) Cabine");
    std::puts("  2) Predio");
    long long alvo = 0, cabine = 0;
    if (!ler_inteiro("Alvo [1-2]: ", 1, 2, alvo)) return;
    if (alvo == 1 && !ler_inteiro("Cabine [1-3]: ", 1, 3, cabine)) return;

    std::puts("  Pressione Enter para parar (Ctrl+C encerra o programa).");
    Instante proximo = Relogio::now();
    unsigned contador = 0;

    while (!encerramento_solicitado()) {
        std::printf("\n--- Leitura #%u ---\n", ++contador);
        if (alvo == 1) {
            const auto r = s.elevador.le_estado_cabine(static_cast<int>(cabine));
            if (r.ok()) imprimir_estado_cabine(static_cast<int>(cabine), r.valor);
            else imprimir_erro(r.status);
        } else {
            const auto r = s.elevador.le_estado_predio();
            if (r.ok()) imprimir_estado_predio(r.valor);
            else imprimir_erro(r.status);
        }

        proximo += std::chrono::milliseconds(PERIODO_LEITURA_CONTINUA_MS);
        const Instante agora = Relogio::now();
        if (proximo < agora) {
            proximo = agora;
        }
        if (esperar_prazo_ou_enter(proximo) != Espera::Prazo) {
            break;
        }
    }
    std::puts("[CLI] Leitura continua encerrada");
}

void menu_elevador(const Sessao &s) {
    while (true) {
        std::puts("\n=== PARTE 3 - MODBUS RTU do simulador (0x03 / 0x10) ===");
        std::puts("   1) le_estado_cabine");
        std::puts("   2) le_estado_predio");
        std::puts("   3) comanda_porta");
        std::puts("   4) escreve_condicao_contorno");
        std::puts("   5) le_chamada_da_fila");
        std::puts("   6) atribui_chamada");
        std::puts("   7) remove_chamada_da_fila");
        std::puts("   8) Leitura bruta de registradores (sem validar o mapa)");
        std::puts("   9) Escrita bruta de registradores (sem validar o mapa)");
        std::puts("  10) Leitura continua (cabine ou predio)");
        std::puts("   0) Voltar");
        std::string linha;
        if (!ler_linha("parte3> ", linha)) return;
        const std::string op = aparar(linha);
        if (op == "1") elevador_le_estado_cabine(s);
        else if (op == "2") elevador_le_estado_predio(s);
        else if (op == "3") elevador_comanda_porta(s);
        else if (op == "4") elevador_escreve_condicao_contorno(s);
        else if (op == "5") elevador_le_chamada(s);
        else if (op == "6") elevador_atribui_chamada(s);
        else if (op == "7") elevador_remove_chamada(s);
        else if (op == "8") elevador_leitura_bruta(s);
        else if (op == "9") elevador_escrita_bruta(s);
        else if (op == "10") elevador_leitura_continua(s);
        else if (op == "0") return;
        else if (!op.empty()) std::puts("[CLI] Opcao invalida");
    }
}

}

void executar_menu_uart(const std::string &caminho_config) {
    const Resultado<Configuracao> cfg = carregar_configuracao(caminho_config);
    if (!cfg.ok()) {
        imprimir_erro(cfg.status);
        return;
    }
    const Configuracao &config = cfg.valor;

    PortaUart porta;
    const Status aberta = porta.abrir(config.uart_dispositivo);
    if (!aberta.ok()) {
        imprimir_erro(aberta);
        return;
    }

    OpcoesRtu opcoes;
    opcoes.timeout_ms = config.timeout_ms;
    opcoes.max_tentativas = config.max_tentativas;
    opcoes.silencio_ms = config.silencio_ms;

    RtuClient cliente(porta, opcoes);
    cliente.definir_trace(criar_trace(config.max_tentativas));
    ElevadorModbus elevador(cliente, config.matricula);
    const Sessao sessao{config, porta, cliente, elevador};

    std::printf("\n[UART] %s aberta: 115200 8N1 | matricula %s | timeout %d ms | ate %d tentativa(s)\n",
                config.uart_dispositivo.c_str(), config.matricula_texto.c_str(), config.timeout_ms,
                config.max_tentativas);

    while (true) {
        std::puts("\n=== COMUNICACAO UART - Entrega 02 ===");
        std::puts("  1) Parte 1 - Protocolo simplificado");
        std::puts("  2) Parte 2 - MODBUS modificado (0x23 / 0x16)");
        std::puts("  3) Parte 3 - MODBUS do simulador (cabines e predio)");
        std::puts("  0) Voltar ao controle da cabine");
        std::string linha;
        if (!ler_linha("uart> ", linha)) {
            break;
        }
        const std::string op = aparar(linha);
        if (op == "1") menu_simplificado(sessao);
        else if (op == "2") menu_modbus_didatico(sessao);
        else if (op == "3") menu_elevador(sessao);
        else if (op == "0") break;
        else if (!op.empty()) std::puts("[CLI] Opcao invalida");
    }

    porta.fechar();
    std::puts("[UART] Porta fechada");
}