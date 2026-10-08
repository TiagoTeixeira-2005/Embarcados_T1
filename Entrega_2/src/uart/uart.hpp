#ifndef UART_HPP
#define UART_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/status.hpp"
#include "common/tempo.hpp"

class PortaUart {
public:
    PortaUart() = default;
    ~PortaUart();

    PortaUart(const PortaUart &) = delete;
    PortaUart &operator=(const PortaUart &) = delete;
    Status abrir(const std::string &dispositivo);
    Status configurar();
    void fechar();

    bool aberta() const { return fd_ >= 0; }
    Status enviar(const uint8_t *dados, size_t tamanho);
    Status enviar(const std::vector<uint8_t> &dados);
    Status receber(std::vector<uint8_t> &destino, size_t quantidade, Instante prazo);
    Status receber_ate_silencio(std::vector<uint8_t> &destino, size_t maximo,
                                Instante prazo_primeiro_byte, int silencio_ms);
    void descartar_entrada();

private:
    int fd_ = -1;
};

#endif
