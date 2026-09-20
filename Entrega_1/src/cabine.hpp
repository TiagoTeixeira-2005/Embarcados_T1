#ifndef CABINE_HPP
#define CABINE_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

enum class Direcao { Livre, Subir, Descer, Freio };

class Cabine {
public:
    static Cabine &instance();

    bool init();
    void cleanup();

    void irParaAndar(int andar);
    void motorManual(Direcao direcao, double dutyPercent);
    void imprimirEstado() const;
    void paradaEmergencia();

    Cabine(const Cabine &) = delete;
    Cabine &operator=(const Cabine &) = delete;

private:
    Cabine() = default;

    void onEncoderEdge(int pinBcm);
    void onCortinaEdge();
    void onSensorAndarEdge();

    static void isrEncA();
    static void isrEncB();
    static void isrCortina();
    static void isrSensorAndar();

    void aplicarDirecao(Direcao direcao);
    void aplicarDuty(double dutyPercent);
    void loopControle();
    void loopDebounceCortina();

    static const char *nomeDirecao(Direcao d);

    // Pinos (BCM)
    static constexpr int PIN_PWM = 13;
    static constexpr int PIN_DIR1 = 22;
    static constexpr int PIN_DIR2 = 23;
    static constexpr int PIN_ENC_A = 20;
    static constexpr int PIN_ENC_B = 21;
    static constexpr int PIN_CORTINA = 26;
    static constexpr int PIN_SENSOR_ANDAR = 0;
    static constexpr int PWM_FREQ_HZ = 1000;

    // Geometria da bancada
    static constexpr int32_t TOLERANCIA_PULSOS = 10;
    static constexpr int32_t POS_MIN_PULSOS = 0;
    static constexpr int32_t POS_MAX_PULSOS = 6000;
    static const std::array<int32_t, 3> FLOOR_POS; // {0, 3000, 6000}

    // Parâmetros do controle simples
    static constexpr double DUTY_ARRANQUE = 15.0;
    static constexpr double DUTY_CRUZEIRO = 55.0;
    static constexpr double DUTY_MINIMA_MOVENDO = 12.0;
    static constexpr int32_t ZONA_DESACELERACAO = 500;
    static constexpr double RAMPA_PASSO_DUTY = 2.0;
    static constexpr int PERIODO_CONTROLE_MS = 20;
    static constexpr int DEBOUNCE_CORTINA_MS = 15;

    // Estado interno
    std::mutex encoderMutex_;     
    std::atomic<int32_t> posicaoPulsos_{0};
    int quadEstado_ = 0;
    std::atomic<int> nivelA_{0};
    std::atomic<int> nivelB_{0};

    std::atomic<int> cortinaEstavel_{0};
    std::atomic<int> cortinaBruta_{0};
    std::atomic<int64_t> cortinaBrutaMudancaMs_{0};

    std::atomic<int> sensorAndarAtivo_{0};
    int32_t bandeirolaEntradaPulsos_ = 0;
    bool bandeirolaEmCurso_ = false;

    mutable std::mutex estadoMutex_; 
    double dutyAtual_ = 0.0;
    Direcao direcaoAtual_ = Direcao::Livre;

    std::atomic<bool> modoAuto_{false};
    std::atomic<int> andarAlvo_{-1};
    std::atomic<bool> movendo_{false};

    std::thread controleThread_;
    std::atomic<bool> controleAtivo_{false};

    std::thread debounceThread_;
    std::atomic<bool> debounceAtivo_{false};
};

#endif 