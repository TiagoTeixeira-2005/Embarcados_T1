#ifndef TEXTO_HPP
#define TEXTO_HPP

#include <string>
inline std::string aparar(const std::string &texto) {
    const size_t inicio = texto.find_first_not_of(" \t\r\n");
    if (inicio == std::string::npos) {
        return std::string();
    }
    const size_t fim = texto.find_last_not_of(" \t\r\n");
    return texto.substr(inicio, fim - inicio + 1);
}

#endif 
