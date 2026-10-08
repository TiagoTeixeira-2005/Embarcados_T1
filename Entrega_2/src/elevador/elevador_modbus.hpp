#ifndef ELEVADOR_MODBUS_HPP
#define ELEVADOR_MODBUS_HPP

#include <cstdint>
#include <vector>

#include "common/matricula.hpp"
#include "common/status.hpp"
#include "elevador/elevador_tipos.hpp"
#include "modbus/rtu_client.hpp"
std::vector<uint8_t> montar_leitura(uint8_t endereco, uint16_t reg, uint16_t qtd,
                                    const Matricula &matricula);
std::vector<uint8_t> montar_escrita(uint8_t endereco, uint16_t reg,
                                    const std::vector<uint16_t> &valores,
                                    const Matricula &matricula);

class ElevadorModbus {
public:
    ElevadorModbus(RtuClient &cliente, const Matricula &matricula);
    Resultado<std::vector<uint16_t>> le_registradores(uint8_t endereco, uint16_t reg, uint16_t qtd);
    Status escreve_registradores(uint8_t endereco, uint16_t reg,
                                 const std::vector<uint16_t> &valores);
    Resultado<EstadoCabine> le_estado_cabine(int cabine);
    Resultado<EstadoPredio> le_estado_predio();
    Status comanda_porta(int cabine, ComandoPorta comando);
    Status escreve_condicao_contorno(uint16_t temp_c_x10,
                                     uint16_t press_hpa);
    Resultado<ChamadaFila> le_chamada_da_fila();
    Status atribui_chamada(uint16_t chamada_id, int cabine);
    Status remove_chamada_da_fila();

private:
    RtuClient &cliente_;
    Matricula matricula_;
};

#endif
