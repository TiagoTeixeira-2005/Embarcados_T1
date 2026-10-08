#ifndef GPIO_HPP
#define GPIO_HPP

#include <wiringPi.h>
#include <softPwm.h>
#include <cstdint>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>

enum class Direcao { Livre, Subir, Descer, Freio };

struct PinConfig {
    int pwm;
    int dir1;
    int dir2;
    int enc_a;
    int enc_b;
    int cortina;
    int sensor_andar;
};

inline int64_t millis_now() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

class GpioModule {
public:
    using CortinaCallback = std::function<void(bool obstruida)>;
    using SensorAndarCallback = std::function<void(bool ativo, int32_t posicao)>;

    explicit GpioModule(const PinConfig &pinos) : pinos_(pinos) {
        wiringPiSetupGpio();

        pinMode(pinos_.dir1, OUTPUT);
        pinMode(pinos_.dir2, OUTPUT);

        softPwmCreate(pinos_.pwm, 0, 10);

        pinMode(pinos_.enc_a, INPUT);
        pinMode(pinos_.enc_b, INPUT);
        pinMode(pinos_.cortina, INPUT);
        pinMode(pinos_.sensor_andar, INPUT);

        enc_estado_ant_ = (digitalRead(pinos_.enc_a) << 1) | digitalRead(pinos_.enc_b);
        cortina_ultima_ms_.store(millis_now());
        cortina_estavel_ = get_cortina();

        set_direction(Direcao::Freio);
        set_duty(0);
    }

    void set_direction(Direcao direcao) {
        switch (direcao) {
            case Direcao::Livre:  digitalWrite(pinos_.dir1, LOW);  digitalWrite(pinos_.dir2, LOW);  break;
            case Direcao::Subir:  digitalWrite(pinos_.dir1, HIGH); digitalWrite(pinos_.dir2, LOW);  break;
            case Direcao::Descer: digitalWrite(pinos_.dir1, LOW);  digitalWrite(pinos_.dir2, HIGH); break;
            case Direcao::Freio:  digitalWrite(pinos_.dir1, HIGH); digitalWrite(pinos_.dir2, HIGH); break;
        }
    }

    void set_duty(double duty_percent) {
        if (duty_percent < 0.0) duty_percent = 0.0;
        if (duty_percent > 100.0) duty_percent = 100.0;
        duty_atual_ = duty_percent;
        int passo = static_cast<int>(duty_percent / 10.0 + 0.5);
        softPwmWrite(pinos_.pwm, passo);
    }

    double get_duty() const { return duty_atual_; }

    int32_t get_position() const { return posicao_.load(); }
    void set_position(int32_t valor) { posicao_.store(valor); }

    bool get_cortina() const { return digitalRead(pinos_.cortina) == HIGH; }
    bool get_sensor_andar() const { return digitalRead(pinos_.sensor_andar) == HIGH; }

    void set_cortina_callback(CortinaCallback cb) { cortina_cb_ = std::move(cb); }
    void set_sensor_andar_callback(SensorAndarCallback cb) { sensor_cb_ = std::move(cb); }

    void on_encoder_edge() {
        std::lock_guard<std::mutex> lock(encoder_mutex_);
        int novo_estado = (digitalRead(pinos_.enc_a) << 1) | digitalRead(pinos_.enc_b);
        int indice = ((enc_estado_ant_ << 2) | novo_estado) & 0xF;
        posicao_.fetch_add(TRANSICOES[indice]);
        enc_estado_ant_ = novo_estado;
    }

    void on_cortina_edge() {
        int64_t agora = millis_now();
        int64_t antes = cortina_ultima_ms_.exchange(agora);
        if (agora - antes < DEBOUNCE_CORTINA_MS) return;

        bool valor = get_cortina();
        if (valor != cortina_estavel_) {
            cortina_estavel_ = valor;
            if (cortina_cb_) cortina_cb_(valor);
        }
    }

    void on_sensor_andar_edge() {
        bool valor = get_sensor_andar();
        int32_t pos = get_position();
        if (sensor_cb_) sensor_cb_(valor, pos);
    }

    void cleanup() {
        set_duty(0);
        set_direction(Direcao::Freio);
    }

private:
    static constexpr int8_t TRANSICOES[16] = {
        0, +1, -1, 0,
       -1, 0,  0, +1,
       +1, 0,  0, -1,
        0, -1, +1, 0,
    };
    static constexpr int64_t DEBOUNCE_CORTINA_MS = 15;

    PinConfig pinos_;

    std::atomic<int32_t> posicao_{0};
    std::mutex encoder_mutex_;
    int enc_estado_ant_ = 0;

    std::atomic<int64_t> cortina_ultima_ms_{0};
    bool cortina_estavel_ = false;

    double duty_atual_ = 0.0;

    CortinaCallback cortina_cb_;
    SensorAndarCallback sensor_cb_;
};

#endif
