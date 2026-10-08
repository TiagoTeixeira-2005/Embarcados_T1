#ifndef TEMPO_HPP
#define TEMPO_HPP

#include <chrono>
#include <climits>

using Relogio = std::chrono::steady_clock;
using Instante = Relogio::time_point;
inline Instante daqui_a_ms(int ms) {
    return Relogio::now() + std::chrono::milliseconds(ms);
}
inline int milissegundos_ate(Instante prazo) {
    const Instante agora = Relogio::now();
    if (prazo <= agora) {
        return 0;
    }
    const long long us =
        std::chrono::duration_cast<std::chrono::microseconds>(prazo - agora).count();
    const long long ms = (us + 999) / 1000;
    return ms > INT_MAX ? INT_MAX : static_cast<int>(ms);
}

#endif 
