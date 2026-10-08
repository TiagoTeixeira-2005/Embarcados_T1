#ifndef TRACE_HPP
#define TRACE_HPP

#include <cstdint>
#include <functional>
#include <vector>

enum class DirecaoTrace { Enviado, Recebido };
using TraceFn = std::function<void(DirecaoTrace direcao,
                                   const std::vector<uint8_t> &bytes,
                                   int tentativa)>;

#endif 
