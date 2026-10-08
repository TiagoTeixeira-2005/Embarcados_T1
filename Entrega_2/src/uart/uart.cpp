#include "uart/uart.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "common/shutdown.hpp"

namespace {

constexpr int TIMEOUT_ESCRITA_MS = 1000;

Status interrompido() {
    return Status::falha(Erro::Interrompido);
}

}

PortaUart::~PortaUart() {
    fechar();
}

Status PortaUart::abrir(const std::string &dispositivo) {
    fechar();

    fd_ = ::open(dispositivo.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        return Status::io(errno, "open " + dispositivo);
    }

    const Status st = configurar();
    if (!st.ok()) {
        fechar();
        return st;
    }
    return Status::sucesso();
}

Status PortaUart::configurar() {
    if (fd_ < 0) {
        return Status::falha(Erro::ErroIO, "porta UART fechada");
    }

    struct termios tio;
    if (tcgetattr(fd_, &tio) != 0) {
        return Status::io(errno, "tcgetattr");
    }

    cfmakeraw(&tio);
    if (cfsetispeed(&tio, B115200) != 0 || cfsetospeed(&tio, B115200) != 0) {
        return Status::io(errno, "cfsetspeed(115200)");
    }

    tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tio.c_cflag |= (CS8 | CLOCAL | CREAD);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd_, TCSANOW, &tio) != 0) {
        return Status::io(errno, "tcsetattr");
    }
    tcflush(fd_, TCIOFLUSH);
    return Status::sucesso();
}

void PortaUart::fechar() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

Status PortaUart::enviar(const uint8_t *dados, size_t tamanho) {
    if (fd_ < 0) {
        return Status::falha(Erro::ErroIO, "porta UART fechada");
    }

    size_t enviados = 0;
    while (enviados < tamanho) {
        if (encerramento_solicitado()) {
            return interrompido();
        }

        const ssize_t n = ::write(fd_, dados + enviados, tamanho - enviados);
        if (n > 0) {
            enviados += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) {
            return Status::io(EIO, "write retornou 0 bytes");
        }

        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            struct pollfd p;
            p.fd = fd_;
            p.events = POLLOUT;
            p.revents = 0;
            const int r = ::poll(&p, 1, TIMEOUT_ESCRITA_MS);
            if (r < 0 && errno == EINTR) {
                continue;
            }
            if (r < 0) {
                return Status::io(errno, "poll(POLLOUT)");
            }
            if (r == 0) {
                return Status::falha(Erro::Timeout, "escrita na UART bloqueada");
            }
            continue;
        }
        return Status::io(errno, "write");
    }
    if (tcdrain(fd_) != 0 && errno == EINTR && encerramento_solicitado()) {
        return interrompido();
    }
    return Status::sucesso();
}

Status PortaUart::enviar(const std::vector<uint8_t> &dados) {
    return enviar(dados.data(), dados.size());
}

Status PortaUart::receber(std::vector<uint8_t> &destino, size_t quantidade, Instante prazo) {
    if (fd_ < 0) {
        return Status::falha(Erro::ErroIO, "porta UART fechada");
    }

    const size_t inicial = destino.size();
    const size_t alvo = inicial + quantidade;
    uint8_t buffer[256];

    while (destino.size() < alvo) {
        if (encerramento_solicitado()) {
            return interrompido();
        }

        struct pollfd p;
        p.fd = fd_;
        p.events = POLLIN;
        p.revents = 0;
        const int r = ::poll(&p, 1, milissegundos_ate(prazo));
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Status::io(errno, "poll(POLLIN)");
        }
        if (r == 0) {
            return Status::falha(Erro::Timeout,
                                 "recebidos " + std::to_string(destino.size() - inicial) +
                                     " de " + std::to_string(quantidade) + " bytes");
        }
        if (p.revents & (POLLERR | POLLNVAL)) {
            return Status::io(EIO, "erro na porta UART (poll)");
        }

        const size_t falta = alvo - destino.size();
        const ssize_t n = ::read(fd_, buffer, std::min(falta, sizeof(buffer)));
        if (n > 0) {
            destino.insert(destino.end(), buffer, buffer + n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
        if (n < 0) {
            return Status::io(errno, "read");
        }
        if (p.revents & POLLHUP) {
            return Status::io(EIO, "porta UART desconectada");
        }
    }
    return Status::sucesso();
}

Status PortaUart::receber_ate_silencio(std::vector<uint8_t> &destino, size_t maximo,
                                       Instante prazo_primeiro_byte, int silencio_ms) {
    const size_t inicial = destino.size();

    Status st = receber(destino, 1, prazo_primeiro_byte);
    if (!st.ok()) {
        return st;
    }

    uint8_t buffer[256];
    while (destino.size() - inicial < maximo) {
        if (encerramento_solicitado()) {
            return interrompido();
        }

        struct pollfd p;
        p.fd = fd_;
        p.events = POLLIN;
        p.revents = 0;
        const int r = ::poll(&p, 1, silencio_ms);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Status::io(errno, "poll(POLLIN)");
        }
        if (r == 0) {
            break;
        }
        if (p.revents & (POLLERR | POLLNVAL)) {
            return Status::io(EIO, "erro na porta UART (poll)");
        }

        const size_t falta = maximo - (destino.size() - inicial);
        const ssize_t n = ::read(fd_, buffer, std::min(falta, sizeof(buffer)));
        if (n > 0) {
            destino.insert(destino.end(), buffer, buffer + n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
        if (n < 0) {
            return Status::io(errno, "read");
        }
        if (p.revents & POLLHUP) {
            break;
        }
    }
    return Status::sucesso();
}

void PortaUart::descartar_entrada() {
    if (fd_ >= 0) {
        tcflush(fd_, TCIFLUSH);
    }
}
