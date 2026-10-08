#include "common/shutdown.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>

namespace {

volatile sig_atomic_t g_encerrar = 0;

void tratar_sigint(int) {
    g_encerrar = 1;
}

} 

void solicitar_encerramento() {
    g_encerrar = 1;
}

bool encerramento_solicitado() {
    return g_encerrar != 0;
}

void limpar_encerramento() {
    g_encerrar = 0;
}

Status instalar_handler_sigint() {
    struct sigaction acao;
    std::memset(&acao, 0, sizeof(acao));
    acao.sa_handler = tratar_sigint;
    sigemptyset(&acao.sa_mask);
    acao.sa_flags = 0; 
    if (sigaction(SIGINT, &acao, nullptr) != 0) {
        return Status::io(errno, "sigaction(SIGINT)");
    }
    return Status::sucesso();
}
