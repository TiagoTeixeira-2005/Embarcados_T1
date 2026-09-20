#ifndef GPIO_HPP
#define GPIO_HPP

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class GpioDriver {
public:
    using IsrHandler = void (*)(void);

    static bool init();      
    static void cleanup();   

    // Saídas on/off ------------------------------------------------------
    static void outputSetup(int pinBcm);
    static void outputWrite(int pinBcm, int value);

    // PWM por software -----------------------------------------------------
    static bool pwmSetup(int pinBcm, int freqHz);
    static void pwmSetDuty(int pinBcm, double dutyPercent); // 0.0 - 100.0
    static void pwmStop(int pinBcm);

    // Entradas por interrupção
    static bool inputInterruptSetup(int pinBcm, const std::string &edge,
                                     IsrHandler handler, bool pullUp);
    static int  inputRead(int pinBcm);

    static uint64_t nowNs();

private:
    class PwmChannel {
    public:
        PwmChannel(int pinBcm, int freqHz);
        ~PwmChannel();
        PwmChannel(const PwmChannel &) = delete;
        PwmChannel &operator=(const PwmChannel &) = delete;

        void setDuty(double dutyPercent);

    private:
        void run();

        int pinBcm_;
        long periodNs_;
        std::atomic<double> dutyPercent_{0.0};
        std::atomic<bool> active_{true};
        std::thread thread_;
    };

    static std::mutex pwmMutex_;
    static std::map<int, std::unique_ptr<PwmChannel>> pwmChannels_;
};

#endif 