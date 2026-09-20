#include <csignal>
#include <signal.h>  
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "cabine.hpp"

namespace {

volatile std::sig_atomic_t g_rodando = 1;

void tratarSigint(int /*sinal*/) {
    g_rodando = 0;
}

void imprimirAjuda() {
    std::cout <<
        "Comandos disponiveis:\n"
        "  andar <0|1|2>                                  - move ate o andar indicado\n"
        "  motor <livre|subir|descer|freio> <duty 0-100>   - acionamento manual\n"
        "  estado                                          - imprime o estado atual\n"
        "  ajuda                                           - mostra esta mensagem\n"
        "  sair                                            - encerra o programa (Ctrl+C)\n";
}

bool direcaoDeString(const std::string &s, Direcao &out) {
    if (s == "livre")  { out = Direcao::Livre;  return true; }
    if (s == "subir")  { out = Direcao::Subir;  return true; }
    if (s == "descer") { out = Direcao::Descer; return true; }
    if (s == "freio")  { out = Direcao::Freio;  return true; }
    return false;
}

} 

int main() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = tratarSigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 
    sigaction(SIGINT, &sa, nullptr);

    Cabine &cabine = Cabine::instance();
    if (!cabine.init()) {
        std::cerr << "Falha ao inicializar a cabine. Encerrando.\n";
        return 1;
    }

    std::cout << "Controle da Cabine 1 - Entrega 1 (C++)\n";
    imprimirAjuda();

    std::string linha;
    while (g_rodando) {
        std::cout << "> " << std::flush;

        if (!std::getline(std::cin, linha)) {
            if (!g_rodando) break; 
            std::cin.clear();
            continue;
        }

        std::istringstream iss(linha);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty()) continue;

        if (cmd == "andar") {
            int andar;
            if (iss >> andar) {
                cabine.irParaAndar(andar);
            } else {
                std::cout << "Uso: andar <0|1|2>\n";
            }

        } else if (cmd == "motor") {
            std::string dirStr;
            double duty;
            if (iss >> dirStr >> duty) {
                Direcao dir;
                if (direcaoDeString(dirStr, dir)) {
                    cabine.motorManual(dir, duty);
                } else {
                    std::cout << "Direcao invalida.\n";
                }
            } else {
                std::cout << "Uso: motor <livre|subir|descer|freio> <duty 0-100>\n";
            }

        } else if (cmd == "estado") {
            cabine.imprimirEstado();

        } else if (cmd == "ajuda") {
            imprimirAjuda();

        } else if (cmd == "sair") {
            break;

        } else {
            std::cout << "Comando desconhecido. Digite 'ajuda'.\n";
        }
    }

    std::cout << "\nEncerrando: zerando PWM, freio e liberando GPIO...\n";
    cabine.cleanup();
    std::cout << "Encerrado.\n";
    return 0;
}
