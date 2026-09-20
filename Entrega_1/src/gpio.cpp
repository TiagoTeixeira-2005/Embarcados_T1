#include "gpio.hpp"

#include <wiringPi.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cerrno>

std::mutex GpioDriver::pwmMutex_;
std::map<int, std::unique_ptr<GpioDriver::PwmChannel>> GpioDriver::pwmChannels_;

// PwM
GpioDriver::PwmChannel::PwmChannel(int pinBcm, int freqHz)
    : pinBcm_(pinBcm), periodNs_(1000000000L / freqHz) {
    pinMode(pinBcm_, OUTPUT);
    digitalWrite(pinBcm_, LOW);
    thread_ = std::thread(&PwmChannel::run, this);
}

GpioDriver::PwmChannel::~PwmChannel() {
    active_ = false;
    if (thread_.joinable()) thread_.join();
    digitalWrite(pinBcm_, LOW);
}

void GpioDriver::PwmChannel::setDuty(double dutyPercent) {
    if (dutyPercent < 0.0) dutyPercent = 0.0;
    if (dutyPercent > 100.0) dutyPercent = 100.0;
    dutyPercent_.store(dutyPercent);
}

void GpioDriver::PwmChannel::run() {
    using namespace std::chrono;

    while (active_.load()) {
        double duty = dutyPercent_.load();

        if (duty <= 0.0) {
            digitalWrite(pinBcm_, LOW);
            std::this_thread::sleep_for(nanoseconds(periodNs_));
            continue;
        }
        if (duty >= 100.0) {
            digitalWrite(pinBcm_, HIGH);
            std::this_thread::sleep_for(nanoseconds(periodNs_));
            continue;
        }

        long onNs  = static_cast<long>(periodNs_ * (duty / 100.0));
        long offNs = periodNs_ - onNs;

        digitalWrite(pinBcm_, HIGH);
        std::this_thread::sleep_for(nanoseconds(onNs));

        digitalWrite(pinBcm_, LOW);
        std::this_thread::sleep_for(nanoseconds(offNs));
    }
}

bool GpioDriver::pwmSetup(int pinBcm, int freqHz) {
    std::lock_guard<std::mutex> lock(pwmMutex_);
    if (pwmChannels_.count(pinBcm)) return false; // ja existe
    pwmChannels_[pinBcm] = std::make_unique<PwmChannel>(pinBcm, freqHz);
    return true;
}

void GpioDriver::pwmSetDuty(int pinBcm, double dutyPercent) {
    std::lock_guard<std::mutex> lock(pwmMutex_);
    auto it = pwmChannels_.find(pinBcm);
    if (it != pwmChannels_.end()) it->second->setDuty(dutyPercent);
}

void GpioDriver::pwmStop(int pinBcm) {
    std::lock_guard<std::mutex> lock(pwmMutex_);
    pwmChannels_.erase(pinBcm);
}

// Saídas on/off
void GpioDriver::outputSetup(int pinBcm) {
    pinMode(pinBcm, OUTPUT);
    digitalWrite(pinBcm, LOW);
}

void GpioDriver::outputWrite(int pinBcm, int value) {
    digitalWrite(pinBcm, value ? HIGH : LOW);
}

// Entradas por interrupção (wiringPiISR)
bool GpioDriver::inputInterruptSetup(int pinBcm, const std::string &edge,
                                      IsrHandler handler, bool pullUp) {
    pinMode(pinBcm, INPUT);
    pullUpDnControl(pinBcm, pullUp ? PUD_UP : PUD_OFF);

    int modo;
    if (edge == "rising")       modo = INT_EDGE_RISING;
    else if (edge == "falling") modo = INT_EDGE_FALLING;
    else                        modo = INT_EDGE_BOTH;

    if (wiringPiISR(pinBcm, modo, handler) < 0) {
        std::fprintf(stderr, "Falha ao registrar interrupcao no GPIO%d: %s\n",
                     pinBcm, std::strerror(errno));
        return false;
    }
    return true;
}

int GpioDriver::inputRead(int pinBcm) {
    return digitalRead(pinBcm);
}

// Utilidades
uint64_t GpioDriver::nowNs() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

bool GpioDriver::init() {
    if (wiringPiSetupGpio() < 0) {
        std::fprintf(stderr, "Falha ao inicializar WiringPi (rodar como root/no grupo gpio?)\n");
        return false;
    }
    return true;
}

void GpioDriver::cleanup() {
    std::lock_guard<std::mutex> lock(pwmMutex_);
    pwmChannels_.clear();
}
