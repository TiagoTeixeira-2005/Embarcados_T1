#ifndef SHUTDOWN_HPP
#define SHUTDOWN_HPP

#include "common/status.hpp"
void solicitar_encerramento();

bool encerramento_solicitado();
void limpar_encerramento();
Status instalar_handler_sigint();

#endif