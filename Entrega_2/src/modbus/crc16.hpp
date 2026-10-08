#ifndef CRC16_HPP
#define CRC16_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

uint16_t calcular_crc16(const uint8_t *dados, size_t tamanho);
uint16_t calcular_crc16(const std::vector<uint8_t> &dados);
void anexar_crc16(std::vector<uint8_t> &trama);
bool verificar_crc16(const uint8_t *trama, size_t tamanho);
bool verificar_crc16(const std::vector<uint8_t> &trama);

#endif
