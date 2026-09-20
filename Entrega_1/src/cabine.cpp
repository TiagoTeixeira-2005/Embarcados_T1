#include "cabine.hpp"
#include "gpio.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

const std::array<int32_t, 3> Cabine::FLOOR_POS = {0, 3000, 6000};

static const int8_t QUAD_TABELA[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
   -1, 0, 0, 1,
    0, 1, -1, 0,
};

Cabine &Cabine::instance() {
    static Cabine instancia; /
    return instancia;
}

const char *Cabine::nomeDirecao(Direcao d) {
    switch (d) {
        case Direcao::Livre:  return "livre";
        case Direcao::Subir:  return "subir";
        case Direcao::Descer: return "descer";
        case Direcao::Freio:  return "freio";
    }
    return "?";
}

void Cabine::isrEncA()        { instance().onEncoderEdge(PIN_ENC_A); }
void Cabine::isrEncB()        { instance().onEncoderEdge(PIN_ENC_B); }
void Cabine::isrCortina()     { instance().onCortinaEdge(); }
void Cabine::isrSensorAndar() { instance().onSensorAndarEdge(); }

// Saídas
void Cabine::aplicarDirecao(Direcao direcao) {
    switch (direcao) {
        case Direcao::Livre:
            GpioDriver::outputWrite(PIN_DIR1, 0);
            GpioDriver::outputWrite(PIN_DIR2, 0);
            break;
        case Direcao::Subir:
            GpioDriver::outputWrite(PIN_DIR1, 1);
            GpioDriver::outputWrite(PIN_DIR2, 0);
            break;
        case Direcao::Descer:
            GpioDriver::outputWrite(PIN_DIR1, 0);
            GpioDriver::outputWrite(PIN_DIR2, 1);
            break;
        case Direcao::Freio:
            GpioDriver::outputWrite(PIN_DIR1, 1);
            GpioDriver::outputWrite(PIN_DIR2, 1);
            break;
    }
    std::lock_guard<std::mutex> lock(estadoMutex_);
    direcaoAtual_ = direcao;
}

void Cabine::aplicarDuty(double dutyPercent) {
    GpioDriver::pwmSetDuty(PIN_PWM, dutyPercent);
    std::lock_guard<std::mutex> lock(estadoMutex_);
    dutyAtual_ = dutyPercent;
}

// Encoder em quadratura
void Cabine::onEncoderEdge(int pinBcm) {
    int valor = GpioDriver::inputRead(pinBcm);

    std::lock_guard<std::mutex> lock(encoderMutex_);
    if (pinBcm == PIN_ENC_A) nivelA_.store(valor);
    else                     nivelB_.store(valor);

    int novoEstado = (nivelA_.load() << 1) | nivelB_.load();
    int indice = ((quadEstado_ << 2) | novoEstado) & 0xF;
    posicaoPulsos_.fetch_add(-QUAD_TABELA[indice]);
    quadEstado_ = novoEstado;
}

// Cortina
void Cabine::onCortinaEdge() {
    int valor = GpioDriver::inputRead(PIN_CORTINA);
    cortinaBruta_.store(valor);
    auto agora = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    cortinaBrutaMudancaMs_.store(agora);
}

void Cabine::loopDebounceCortina() {
    using namespace std::chrono;
    while (debounceAtivo_.load()) {
        std::this_thread::sleep_for(milliseconds(5)); // periodico, nao busy-wait

        int bruto = cortinaBruta_.load();
        int estavel = cortinaEstavel_.load();
        auto agora = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        auto desdeMudanca = agora - cortinaBrutaMudancaMs_.load();

        if (bruto != estavel && desdeMudanca >= DEBOUNCE_CORTINA_MS) {
            cortinaEstavel_.store(bruto);
            if (bruto) std::printf("[CORTINA] Obstrucao detectada (porta bloqueada)\n");
            else       std::printf("[CORTINA] Porta liberada\n");
            std::fflush(stdout);
        }
    }
}

// Sensor de Andar
void Cabine::onSensorAndarEdge() {
    int valor = GpioDriver::inputRead(PIN_SENSOR_ANDAR);
    int32_t pos = posicaoPulsos_.load();
    sensorAndarAtivo_.store(valor);

    if (valor == 1 && !bandeirolaEmCurso_) {
        bandeirolaEntradaPulsos_ = pos;
        bandeirolaEmCurso_ = true;
        std::printf("[SENSOR_ANDAR] Entrada na bandeirola em %d pulsos\n", pos);
        std::fflush(stdout);
        return;
    }

    if (valor == 0 && bandeirolaEmCurso_) {
        int32_t saida = pos;
        int32_t centro = (bandeirolaEntradaPulsos_ + saida) / 2;
        bandeirolaEmCurso_ = false;

        int32_t melhorRef = *std::min_element(
            FLOOR_POS.begin(), FLOOR_POS.end(),
            [centro](int32_t a, int32_t b) {
                return std::abs(centro - a) < std::abs(centro - b);
            });
        int32_t erro = centro - melhorRef;

        std::printf("[SENSOR_ANDAR] Saida da bandeirola em %d pulsos | "
                    "centro estimado = %d | andar de referencia = %d mm | "
                    "erro = %d mm\n",
                    saida, centro, melhorRef, erro);
        std::fflush(stdout);
    }
}

// Thread de controle de posição
void Cabine::loopControle() {
    while (controleAtivo_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(PERIODO_CONTROLE_MS));

        int32_t pos = posicaoPulsos_.load();
        double dutyAtual;
        Direcao dirAtual;
        {
            std::lock_guard<std::mutex> lock(estadoMutex_);
            dutyAtual = dutyAtual_;
            dirAtual = direcaoAtual_;
        }

        bool ultrapassouLimite =
            (dirAtual == Direcao::Subir && pos >= POS_MAX_PULSOS) ||
            (dirAtual == Direcao::Descer && pos <= POS_MIN_PULSOS);
        if (ultrapassouLimite) {
            aplicarDuty(0.0);
            aplicarDirecao(Direcao::Freio);
            modoAuto_.store(false);
            movendo_.store(false);
            std::printf("[CABINE] Fim de curso atingido (posicao=%d pulsos) - motor parado por seguranca\n", pos);
            std::fflush(stdout);
            continue;
        }

        if (!modoAuto_.load()) continue;

        int andarIdx = andarAlvo_.load();
        if (andarIdx < 0) continue;

        int32_t alvo = FLOOR_POS[andarIdx];
        int32_t erro = alvo - pos;

        if (std::abs(erro) <= TOLERANCIA_PULSOS) {
            aplicarDuty(0.0);
            aplicarDirecao(Direcao::Freio);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            aplicarDirecao(Direcao::Livre);
            modoAuto_.store(false);
            movendo_.store(false);
            std::printf("[CABINE] Chegou ao andar %d (posicao=%d pulsos, erro=%d mm)\n",
                        andarIdx, pos, erro);
            std::fflush(stdout);
            continue;
        }

        Direcao dirNecessaria = (erro > 0) ? Direcao::Subir : Direcao::Descer;
        int32_t distAbs = std::abs(erro);


        if (dirAtual != dirNecessaria || !movendo_.load()) {
            aplicarDirecao(dirNecessaria);
            movendo_.store(true);
        }

        double dutyDesejado;
        if (distAbs > ZONA_DESACELERACAO) {
            dutyDesejado = DUTY_CRUZEIRO;
        } else {
            double proporcao = static_cast<double>(distAbs) / ZONA_DESACELERACAO;
            dutyDesejado = DUTY_MINIMA_MOVENDO + proporcao * (DUTY_CRUZEIRO - DUTY_MINIMA_MOVENDO);
        }

        double novoDuty;
        if (dutyAtual < DUTY_ARRANQUE) {
            novoDuty = DUTY_ARRANQUE; 
        } else if (dutyAtual < dutyDesejado) {
            novoDuty = std::min(dutyAtual + RAMPA_PASSO_DUTY, dutyDesejado);
        } else {
            novoDuty = dutyDesejado;
        }

        aplicarDuty(std::min(novoDuty, 100.0));
    }
}

bool Cabine::init() {
    if (!GpioDriver::init()) return false;

    GpioDriver::outputSetup(PIN_DIR1);
    GpioDriver::outputSetup(PIN_DIR2);
    aplicarDirecao(Direcao::Livre);

    GpioDriver::pwmSetup(PIN_PWM, PWM_FREQ_HZ);
    aplicarDuty(0.0);

    GpioDriver::inputInterruptSetup(PIN_ENC_A, "both", &Cabine::isrEncA, true);
    GpioDriver::inputInterruptSetup(PIN_ENC_B, "both", &Cabine::isrEncB, true);
    GpioDriver::inputInterruptSetup(PIN_CORTINA, "both", &Cabine::isrCortina, true);
    GpioDriver::inputInterruptSetup(PIN_SENSOR_ANDAR, "both", &Cabine::isrSensorAndar, true);

    debounceAtivo_.store(true);
    debounceThread_ = std::thread(&Cabine::loopDebounceCortina, this);

    controleAtivo_.store(true);
    controleThread_ = std::thread(&Cabine::loopControle, this);

    std::printf("[CABINE] Inicializada. Posicao = 0 pulsos (andar 0).\n");
    return true;
}

void Cabine::cleanup() {
    paradaEmergencia();

    controleAtivo_.store(false);
    if (controleThread_.joinable()) controleThread_.join();

    debounceAtivo_.store(false);
    if (debounceThread_.joinable()) debounceThread_.join();

    GpioDriver::cleanup();
}

void Cabine::irParaAndar(int andar) {
    if (andar < 0 || andar > 2) {
        std::printf("Andar invalido. Use 0, 1 ou 2.\n");
        return;
    }
    int32_t alvoPulsos = FLOOR_POS[andar];
    if (alvoPulsos < POS_MIN_PULSOS || alvoPulsos > POS_MAX_PULSOS) {
        std::printf("Alvo fora dos fins de curso da bancada.\n");
        return;
    }
    andarAlvo_.store(andar);
    modoAuto_.store(true);
    std::printf("[CABINE] Comando: ir para o andar %d (%d pulsos)\n", andar, alvoPulsos);
}

void Cabine::motorManual(Direcao direcao, double dutyPercent) {
    modoAuto_.store(false);
    movendo_.store(false);
    aplicarDirecao(direcao);
    aplicarDuty(dutyPercent);
    std::printf("[CABINE] Modo manual: direcao=%s duty=%.1f%%\n",
                nomeDirecao(direcao), dutyPercent);
}

void Cabine::paradaEmergencia() {
    modoAuto_.store(false);
    aplicarDuty(0.0);
    aplicarDirecao(Direcao::Freio);
}

void Cabine::imprimirEstado() const {
    int32_t pos = posicaoPulsos_.load();

    int andarEstimado = 0;
    int32_t melhorDist = std::abs(pos - FLOOR_POS[0]);
    for (int i = 1; i < 3; i++) {
        int32_t d = std::abs(pos - FLOOR_POS[i]);
        if (d < melhorDist) { melhorDist = d; andarEstimado = i; }
    }
    bool nivelado = melhorDist <= TOLERANCIA_PULSOS;

    double duty;
    Direcao dir;
    {
        std::lock_guard<std::mutex> lock(estadoMutex_);
        duty = dutyAtual_;
        dir = direcaoAtual_;
    }

    std::printf("---- Estado da Cabine 1 ----\n");
    std::printf("  Posicao        : %d pulsos (%.1f mm)\n", pos, static_cast<double>(pos));
    std::printf("  Andar estimado : %d (dist=%d pulsos)\n", andarEstimado, melhorDist);
    std::printf("  Nivelado (+-10mm): %s\n", nivelado ? "sim" : "nao");
    std::printf("  Direcao atual  : %s\n", nomeDirecao(dir));
    std::printf("  Duty atual     : %.1f%%\n", duty);
    std::printf("  Cortina        : %s\n", cortinaEstavel_.load() ? "obstruida" : "livre");
    std::printf("  Sensor de Andar: %s\n", sensorAndarAtivo_.load() ? "na bandeirola" : "fora da bandeirola");
    std::printf("  Modo automatico: %s\n", modoAuto_.load() ? "ativo" : "inativo");
    std::printf("-----------------------------\n");
    std::fflush(stdout);
}
