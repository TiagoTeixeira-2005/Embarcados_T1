#include "gpio.hpp"
#include "cli/cli.hpp"
#include "common/shutdown.hpp"

#include <csignal>
#include <signal.h>  
#include <poll.h>    
#include <unistd.h>  
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

using namespace std;
static const PinConfig PINOS = {
    /*pwm*/          13,
    /*dir1*/         22,
    /*dir2*/         23,
    /*enc_a*/        20,
    /*enc_b*/        21,
    /*cortina*/      26,
    /*sensor_andar*/  0,
};

static GpioModule g_gpio(PINOS);
static void isr_enc_a()        { g_gpio.on_encoder_edge(); }
static void isr_enc_b()        { g_gpio.on_encoder_edge(); }
static void isr_cortina()      { g_gpio.on_cortina_edge(); }
static void isr_sensor_andar() { g_gpio.on_sensor_andar_edge(); }
static const map<int, int32_t> ANDARES = {{0, 0}, {1, 3000}, {2, 6000}};
static constexpr int32_t TOLERANCIA_MM = 10;
static constexpr int32_t LIMITE_INF = 0;
static constexpr int32_t LIMITE_SUP = 6000;
static constexpr double DUTY_RAPIDO = 55.0;
static constexpr double DUTY_LENTO = 15.0;
static constexpr int32_t ZONA_LENTA = 500;
static constexpr double PASSO_RAMPA = 2.0;
static constexpr int PERIODO_MS = 20;
struct MedicaoBandeirola {
    bool valida = false;
    int32_t entrada = 0, saida = 0;
    double centro = 0;
    int andar = 0;
    double erro = 0;
};
static MedicaoBandeirola g_ultima_bandeirola;
static int32_t g_bandeirola_entrada = 0;
static bool g_bandeirola_em_curso = false;
static atomic<bool> g_continuar{true};
static volatile sig_atomic_t g_sigint = 0;

static void tratar_sigint(int /*sinal*/) {
    g_sigint = 1;
    g_continuar.store(false); 
    solicitar_encerramento();
}
static int andar_mais_proximo(int32_t pos) {
    int melhor = ANDARES.begin()->first;
    int32_t melhor_dist = abs(pos - ANDARES.begin()->second);
    for (const auto &par : ANDARES) {
        int32_t d = abs(pos - par.second);
        if (d < melhor_dist) { melhor_dist = d; melhor = par.first; }
    }
    return melhor;
}

static bool esta_nivelado(int32_t pos) {
    int andar = andar_mais_proximo(pos);
    return abs(pos - ANDARES.at(andar)) <= TOLERANCIA_MM;
}

static bool fim_de_curso(Direcao direcao, int32_t pos) {
    if (direcao == Direcao::Subir  && pos >= LIMITE_SUP) return true;
    if (direcao == Direcao::Descer && pos <= LIMITE_INF) return true;
    return false;
}

static void dormir_ms(int ms) {
    this_thread::sleep_for(chrono::milliseconds(ms));
}

static void cabine_status();
enum class ComandoDuranteMovimento { Nenhum, Frear, Continuar };

static ComandoDuranteMovimento verificar_comando_durante_movimento(int ms_espera) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int rc = poll(&pfd, 1, ms_espera);
    if (rc <= 0 || !(pfd.revents & POLLIN)) {
        return ComandoDuranteMovimento::Nenhum; 
    }

    string linha;
    if (!getline(cin, linha)) {
        return ComandoDuranteMovimento::Nenhum;
    }

    istringstream iss(linha);
    string cmd;
    iss >> cmd;
    for (auto &c : cmd) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    if (cmd.empty()) return ComandoDuranteMovimento::Nenhum;

    if (cmd == "freio") {
        printf("\n[CABINE] Freio solicitado durante o movimento - parando.\n");
        return ComandoDuranteMovimento::Frear;
    }

    if (cmd == "motor") {
        string sub;
        iss >> sub;
        for (auto &c : sub) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (sub == "freio") {
            printf("\n[CABINE] Freio solicitado durante o movimento - parando.\n");
            return ComandoDuranteMovimento::Frear;
        }
    }

    if (cmd == "estado") {
        printf("\n");
        cabine_status();
        printf("> ");
        fflush(stdout);
        return ComandoDuranteMovimento::Continuar;
    }

    printf("\n[AVISO] Comando '%s' ignorado: a cabine ainda esta em movimento. "
           "Digite 'freio' para interromper, 'estado' para consultar, ou espere terminar.\n> ",
           linha.c_str());
    fflush(stdout);
    return ComandoDuranteMovimento::Continuar;
}
static void anunciar_se_chegou_andar(int32_t pos, int &ultimo_anunciado) {
    if (!esta_nivelado(pos)) return;
    int andar = andar_mais_proximo(pos);
    if (andar == ultimo_anunciado) return;
    ultimo_anunciado = andar;
    printf("\n[CABINE] Passou pelo andar %d durante o movimento manual (posicao=%d pulsos, nivelado)\n> ",
           andar, pos);
    fflush(stdout);
}
static void ao_mudar_cortina(bool obstruida) {
    printf("\n[CORTINA] %s\n> ",
           obstruida ? "Obstrucao detectada (porta bloqueada)" : "Porta liberada");
    fflush(stdout);
}

static void ao_mudar_sensor_andar(bool ativo, int32_t pos) {
    if (ativo && !g_bandeirola_em_curso) {
        g_bandeirola_entrada = pos;
        g_bandeirola_em_curso = true;
        printf("\n[SENSOR_ANDAR] Entrada na bandeirola em %d pulsos\n> ", pos);
        fflush(stdout);
        return;
    }
    if (!ativo && g_bandeirola_em_curso) {
        int32_t saida = pos;
        g_bandeirola_em_curso = false;

        double centro = (g_bandeirola_entrada + saida) / 2.0;
        int andar = andar_mais_proximo(static_cast<int32_t>(centro));
        double erro = centro - ANDARES.at(andar);
        g_ultima_bandeirola = {true, g_bandeirola_entrada, saida, centro, andar, erro};

        printf("\n[SENSOR_ANDAR] Saida da bandeirola em %d pulsos | centro estimado = %.0f | "
               "andar de referencia = %d mm | erro = %.0f mm\n> ",
               saida, centro, ANDARES.at(andar), erro);
        fflush(stdout);
    }
}
static void cabine_andar(int andar) {
    auto it = ANDARES.find(andar);
    if (it == ANDARES.end()) {
        printf("Andar invalido. Disponiveis: 0, 1, 2\n");
        return;
    }
    int32_t alvo = it->second;
    int32_t pos = g_gpio.get_position();
    int32_t erro = alvo - pos;

    if (abs(erro) <= TOLERANCIA_MM) {
        printf("Cabine ja esta no andar %d (%d pulsos).\n", andar, pos);
        return;
    }

    Direcao direcao = (erro > 0) ? Direcao::Subir : Direcao::Descer;
    printf("[CABINE] Comando: ir para o andar %d (%d pulsos)\n", andar, alvo);
    g_gpio.set_direction(direcao);

    double duty = 0.0;
    bool parado_manualmente = false;
    while (g_continuar.load()) {
        pos = g_gpio.get_position();
        erro = alvo - pos;
        if (abs(erro) <= TOLERANCIA_MM) break;

        if (fim_de_curso(direcao, pos)) {
            printf("[CABINE] Fim de curso atingido (posicao=%d pulsos) - motor parado por seguranca\n", pos);
            break;
        }

        Direcao nova_direcao = (erro > 0) ? Direcao::Subir : Direcao::Descer;
        if (nova_direcao != direcao) {
            g_gpio.set_duty(0);
            g_gpio.set_direction(Direcao::Freio);
            dormir_ms(100);
            direcao = nova_direcao;
            duty = 0.0;
            g_gpio.set_direction(direcao);
        }

        double duty_alvo = (abs(erro) > ZONA_LENTA) ? DUTY_RAPIDO : DUTY_LENTO;
        if (duty < duty_alvo)      duty = min(duty + PASSO_RAMPA, duty_alvo);
        else if (duty > duty_alvo) duty = max(duty - PASSO_RAMPA, duty_alvo);

        g_gpio.set_duty(duty);

        if (verificar_comando_durante_movimento(PERIODO_MS) == ComandoDuranteMovimento::Frear) {
            parado_manualmente = true;
            break;
        }
    }

    g_gpio.set_duty(0);
    g_gpio.set_direction(Direcao::Freio);

    pos = g_gpio.get_position();
    int andar_real = andar_mais_proximo(pos);
    bool nivelado = esta_nivelado(pos);

    if (parado_manualmente) {
        printf("[CABINE] Movimento interrompido manualmente (posicao=%d pulsos, andar mais proximo=%d) - %s\n",
               pos, andar_real, nivelado ? "nivelado" : "fora da tolerancia");
    } else {
        printf("[CABINE] Chegou ao andar %d (posicao=%d pulsos, erro=%d mm) - %s\n",
               andar_real, pos, pos - ANDARES.at(andar_real),
               nivelado ? "nivelado" : "fora da tolerancia");
    }
}
static void cabine_motor(Direcao direcao, double duty_alvo, double duracao_segundos, bool duracao_definida) {
    int32_t pos = g_gpio.get_position();
    if (fim_de_curso(direcao, pos)) {
        printf("Movimento bloqueado: ja esta no fim de curso.\n");
        return;
    }

    if (duracao_definida) {
        printf("[CABINE] Modo manual: direcao=%s duty_alvo=%.1f%% duracao=%.1fs\n",
               direcao == Direcao::Subir ? "subir" : "descer", duty_alvo, duracao_segundos);
    } else {
        printf("[CABINE] Modo manual: direcao=%s duty_alvo=%.1f%% duracao=indefinida "
               "(digite 'freio' ou Ctrl+C para parar)\n",
               direcao == Direcao::Subir ? "subir" : "descer", duty_alvo);
    }
    g_gpio.set_direction(direcao);

    double duty = 0.0;
    bool parou_por_seguranca = false;
    bool parou_por_freio_manual = false;
    int ultimo_andar_anunciado = -1;

    anunciar_se_chegou_andar(pos, ultimo_andar_anunciado);

    while (g_continuar.load() && duty < duty_alvo) {
        int32_t pos_atual = g_gpio.get_position();
        if (fim_de_curso(direcao, pos_atual)) { parou_por_seguranca = true; break; }
        anunciar_se_chegou_andar(pos_atual, ultimo_andar_anunciado);

        duty = min(duty + PASSO_RAMPA, duty_alvo);
        g_gpio.set_duty(duty);

        if (verificar_comando_durante_movimento(PERIODO_MS) == ComandoDuranteMovimento::Frear) {
            parou_por_freio_manual = true;
            break;
        }
    }

    if (!parou_por_seguranca && !parou_por_freio_manual) {
        auto inicio = chrono::steady_clock::now();
        while (g_continuar.load()) {
            int32_t pos_atual = g_gpio.get_position();
            anunciar_se_chegou_andar(pos_atual, ultimo_andar_anunciado);

            if (duracao_definida) {
                double decorrido = chrono::duration<double>(chrono::steady_clock::now() - inicio).count();
                if (decorrido >= duracao_segundos) break;
            }
            if (fim_de_curso(direcao, pos_atual)) { parou_por_seguranca = true; break; }

            if (verificar_comando_durante_movimento(PERIODO_MS) == ComandoDuranteMovimento::Frear) {
                parou_por_freio_manual = true;
                break;
            }
        }
    }

    if (parou_por_seguranca) {
        g_gpio.set_duty(0);
        g_gpio.set_direction(Direcao::Freio);
        printf("[CABINE] Fim de curso atingido - motor parado por seguranca\n");
    } else if (parou_por_freio_manual) {
        g_gpio.set_duty(0);
        g_gpio.set_direction(Direcao::Freio);
        printf("[CABINE] Freio aplicado (interrupcao manual durante o movimento)\n");
    } else {
        while (duty > 0) {
            duty = max(duty - PASSO_RAMPA, 0.0);
            g_gpio.set_duty(duty);
            dormir_ms(PERIODO_MS);
        }
        g_gpio.set_direction(Direcao::Freio);
    }

    printf("[CABINE] Motor parado | posicao=%d pulsos\n", g_gpio.get_position());
}

static void cabine_livre() {
    g_gpio.set_duty(0);
    g_gpio.set_direction(Direcao::Livre);
    printf("[CABINE] Motor livre.\n");
}

static void cabine_freio() {
    g_gpio.set_duty(0);
    g_gpio.set_direction(Direcao::Freio);
    printf("[CABINE] Freio acionado.\n");
}

static void cabine_status() {
    int32_t pos = g_gpio.get_position();
    int andar = andar_mais_proximo(pos);
    bool nivelado = esta_nivelado(pos);

    printf("---- Estado da Cabine 1 ----\n");
    printf("  Posicao          : %d pulsos (%d mm)\n", pos, pos);
    printf("  Andar estimado   : %d (dist=%d pulsos)\n", andar, abs(pos - ANDARES.at(andar)));
    printf("  Nivelado (+-10mm): %s\n", nivelado ? "sim" : "nao");
    printf("  Duty atual       : %.1f%%\n", g_gpio.get_duty());
    printf("  Cortina          : %s\n", g_gpio.get_cortina() ? "obstruida" : "livre");
    printf("  Sensor de Andar  : %s\n", g_gpio.get_sensor_andar() ? "na bandeirola" : "fora da bandeirola");
    if (g_ultima_bandeirola.valida) {
        printf("  Ultima bandeirola: andar %d | centro=%.1f pulsos | erro=%.1f mm\n",
               g_ultima_bandeirola.andar, g_ultima_bandeirola.centro, g_ultima_bandeirola.erro);
    }
    printf("-----------------------------\n");
}

static void imprimir_ajuda() {
    cout <<
        "Comandos disponiveis:\n"
        "  andar <0|1|2>                          - move ate o andar (automatico, com feedback do encoder)\n"
        "  motor subir <duty 0-100> [segundos]    - sobe manualmente (indefinido se omitir os segundos)\n"
        "  motor descer <duty 0-100> [segundos]   - desce manualmente (indefinido se omitir os segundos)\n"
        "  motor livre                             - solta o motor (DIR1=DIR2=0)\n"
        "  motor freio                             - trava o motor (DIR1=DIR2=1)\n"
        "  estado                                   - imprime o estado atual da cabine\n"
        "  uart [arquivo]                           - menu UART/MODBUS da Entrega 2 (padrao: config.ini)\n"
        "  ajuda                                    - mostra esta mensagem\n"
        "  sair                                     - encerra o programa (equivalente a Ctrl+C)\n";
}

int main() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = tratar_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    sigaction(SIGINT, &sa, nullptr);

    g_gpio.set_cortina_callback(ao_mudar_cortina);
    g_gpio.set_sensor_andar_callback(ao_mudar_sensor_andar);

    wiringPiISR(PINOS.enc_a, INT_EDGE_BOTH, &isr_enc_a);
    wiringPiISR(PINOS.enc_b, INT_EDGE_BOTH, &isr_enc_b);
    wiringPiISR(PINOS.cortina, INT_EDGE_BOTH, &isr_cortina);
    wiringPiISR(PINOS.sensor_andar, INT_EDGE_BOTH, &isr_sensor_andar);

    printf("[CABINE] Inicializada. Posicao = 0 pulsos (andar 0).\n");
    cout << "Controle da Cabine 1 - Entrega 1 (C++, versao em 2 arquivos)\n";
    imprimir_ajuda();

    string linha;
    while (!g_sigint) {
        cout << "> " << flush;

        if (!getline(cin, linha)) {
            if (g_sigint) break;
            cin.clear();
            continue;
        }

        istringstream iss(linha);
        string cmd;
        iss >> cmd;
        if (cmd.empty()) continue;

        if (cmd == "andar") {
            int andar;
            if (iss >> andar) cabine_andar(andar);
            else cout << "Uso: andar <0|1|2>\n";

        } else if (cmd == "motor") {
            string sub;
            iss >> sub;

            if (sub == "subir" || sub == "descer") {
                double duty;
                if (!(iss >> duty)) {
                    cout << "Uso: motor " << sub << " <duty 0-100> [segundos]\n";
                    continue;
                }
                double segundos = 0.0;
                bool tem_duracao = static_cast<bool>(iss >> segundos);
                Direcao d = (sub == "subir") ? Direcao::Subir : Direcao::Descer;
                cabine_motor(d, duty, segundos, tem_duracao);
            } else if (sub == "livre") {
                cabine_livre();
            } else if (sub == "freio") {
                cabine_freio();
            } else {
                cout << "Uso: motor <subir|descer> <duty> <segundos> | motor <livre|freio>\n";
            }

        } else if (cmd == "estado") {
            cabine_status();

        } else if (cmd == "uart") {
            string caminho = "config.ini";
            string argumento;
            if (iss >> argumento) caminho = argumento;
            executar_menu_uart(caminho);

        } else if (cmd == "ajuda") {
            imprimir_ajuda();

        } else if (cmd == "sair") {
            break;

        } else {
            cout << "Comando desconhecido. Digite 'ajuda'.\n";
        }
    }

    cout << "\nEncerrando: zerando PWM, freio e liberando GPIO...\n";
    g_gpio.cleanup();
    cout << "Encerrado.\n";
    return 0;
}
