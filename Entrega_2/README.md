# Trabalho 1 — Entrega 2: Comunicação UART / MODBUS

**Disciplina:** Fundamentos de Sistemas Embarcados (2026/2)
**Linguagem:** C++17
**Biblioteca de GPIO:** WiringPi
**Comunicação UART:** POSIX (`termios` e `poll`)

Este README apresenta como a comunicação entre a Raspberry Pi e a ESP32 foi implementada na Entrega 2, que aproveita o programa desenvolvido na Entrega 1 e acrescenta a comunicação UART, permitindo que a Raspberry Pi envie comandos para a ESP32 e receba respostas.

Foram implementadas as três partes solicitadas no trabalho:

* **Parte 1:** protocolo simplificado;
* **Parte 2:** protocolo MODBUS modificado;
* **Parte 3:** comunicação MODBUS com o simulador do elevador.

## Alunos
|Matrícula | Aluno |
| -- | -- |
| 23/1037656  |  Arthur Guilherme Aquino Santos |
| 23/1026581  |  Tiago Lemes Teixeira |

## Sumário

* [1. Como compilar e executar](#1-como-compilar-e-executar)

  * [1.1. Dependências](#11-dependências)
  * [1.2. Compilação](#12-compilação)
  * [1.3. Execução](#13-execução)
* [2. Configuração](#2-configuração)
* [3. Arquitetura](#3-arquitetura)

  * [3.1. Estrutura de arquivos](#31-estrutura-de-arquivos)
  * [3.2. Relação com a Entrega 1](#32-relação-com-a-entrega-1)
* [4. Funcionamento do programa](#4-funcionamento-do-programa)

  * [4.1. Interface de terminal](#41-interface-de-terminal)
  * [4.2. Parte 1: protocolo simplificado](#42-parte-1-protocolo-simplificado)
  * [4.3. Parte 2: MODBUS modificado](#43-parte-2-modbus-modificado)
  * [4.4. Parte 3: MODBUS do simulador](#44-parte-3-modbus-do-simulador)
  * [4.5. Timeout e retentativas](#45-timeout-e-retentativas)
  * [4.6. Tratamento de SIGINT](#46-tratamento-de-sigint)
* [5. Requisitos](#5-requisitos)
* [6. Testes de funcionamento](#6-testes-de-funcionamento)
* [7. Gravação](#7-gravação)

---

# 1. Como compilar e executar

## 1.1. Dependências

É necessário possuir:

* Raspberry Pi;
* `g++` com suporte a C++17;
* `make`;
* WiringPi.

Para verificar se a WiringPi está instalada:

```bash
gpio -v
```

A comunicação UART utiliza recursos do próprio sistema Linux, principalmente:

* `termios`, para configurar a porta serial;
* `poll()`, para aguardar respostas sem utilizar busy-wait.

## 1.2. Compilação

Na pasta do projeto:

```bash
make
```

O comando gera o executável:

```text
elevador_cabine1
```

Para remover os arquivos gerados:

```bash
make clean
```

## 1.3. Execução

Primeiro, deve ser informado os 6 últimos dígitos da matrícula no arquivo **config.ini** e, em seguida, execute:

```bash
./elevador_cabine1
```

O programa continua apresentando os comandos da Entrega 1. Para acessar as funções de comunicação, utilize:

```text
> uart
```

---

# 2. Configuração

A comunicação UART utiliza um arquivo `config.ini` para armazenar informações que podem variar entre as Raspberry Pis.

Exemplo:

```ini
uart_dispositivo=/dev/serial0
matricula=037656
timeout_ms=300
max_tentativas=3
silencio_ms=20
endereco_modbus_didatico=0x01
```

As principais configurações são:

| Configuração       | Descrição                                                           |
| ------------------ | ------------------------------------------------------------------- |
| `uart_dispositivo` | Porta serial utilizada pela Raspberry Pi                            |
| `matricula`        | Seis últimos dígitos da matrícula                                   |
| `timeout_ms`       | Tempo máximo de espera por uma resposta                             |
| `max_tentativas`   | Número máximo de tentativas                                         |
| `silencio_ms`      | Tempo utilizado para identificar o final de uma mensagem da Parte 2 |

A comunicação UART utiliza:

```text
115200 bps
8 bits
sem paridade
1 stop bit
sem controle de fluxo
```

A matrícula possui exatamente seis dígitos e é enviada como **bytes numéricos**, e não como caracteres ASCII.

Por exemplo, para:

```text
037656
```

são enviados:

```text
0x00 0x03 0x07 0x06 0x05 0x06
```

---

# 3. Arquitetura

## 3.1. Estrutura de arquivos

A Entrega 2 adiciona novos módulos ao projeto da Entrega 1:

```text
.
├── Makefile
├── README.md
├── config.ini
└── src/
    ├── cli/
    │   └── cli.hpp / .cpp
    ├── common/
    │   ├── bytes.hpp
    │   ├── hexdump.hpp
    │   ├── matricula.hpp
    │   ├── shutdown.hpp / .cpp
    │   ├── status.hpp
    │   ├── tempo.hpp
    │   ├── texto.hpp
    │   └── trace.hpp
    ├── config/
    │   └── config.hpp / .cpp
    ├── elevador/
    │   ├── elevador_modbus.hpp / .cpp
    │   └── elevador_tipos.hpp
    ├── modbus/
    │   ├── crc16.hpp / .cpp
    │   └── rtu_client.hpp / .cpp
    ├── protocolo/
    │   ├── protocolo_modbus.hpp / .cpp
    │   └── protocolo_simplificado.hpp / .cpp
    ├── uart/
    │   └── uart.hpp / .cpp
    ├── gpio.hpp
    │   └── Controle dos GPIOs da Entrega 1
    └── main.cpp
        └── Programa principal e controle da cabine

```

Os principais módulos são:

| Módulo                               | Função                                                          |
| ------------------------------------ | --------------------------------------------------------------- |
| `gpio.hpp`                           | Controle dos GPIOs da cabine                                    |
| `common/`                            | Erros, bytes, matrícula, tempo, trace e encerramento (Ctrl+C)   |
| `uart/`                              | Abre, configura, envia e recebe dados pela UART                 |
| `modbus/crc16.*`                     | Calcula e verifica o CRC                                        |
| `modbus/rtu_client.*`                | Controla as transações, timeout e tentativas                    |
| `protocolo/protocolo_simplificado.*` | Implementa a Parte 1                                            |
| `protocolo/protocolo_modbus.*`       | Implementa a Parte 2                                            |
| `elevador/elevador_modbus.*`         | Implementa a Parte 3                                            |
| `config/`                            | Lê o arquivo `config.ini`                                       |
| `cli/`                               | Menus e interação com o usuário                                 |

## 3.2. Relação com a Entrega 1

A Entrega 2 **mantém o controle da cabine desenvolvido na Entrega 1**.

O arquivo:

```text
gpio.hpp
```

continua responsável pelos GPIOs, PWM, encoder, cortina e sensor de andar.

O `main.cpp` continua responsável pela lógica da cabine, mas agora também reconhece o comando:

```text
uart
```

Esse comando abre o menu de comunicação com a ESP32.

A UART é aberta somente quando o menu `uart` é utilizado e é fechada quando o usuário sai desse menu.

---

# 4. Funcionamento do programa

## 4.1. Interface de terminal

Ao executar:

```text
> uart
```

é apresentado:

```text
=== COMUNICACAO UART - Entrega 02 ===

1) Parte 1 - Protocolo simplificado
2) Parte 2 - MODBUS modificado
3) Parte 3 - MODBUS do simulador
0) Voltar
```

Cada opção apresenta os comandos correspondentes àquela parte.

Durante as operações são mostrados:

* os bytes enviados;
* os bytes recebidos;
* o número da tentativa;
* os valores interpretados da resposta.

Exemplo:

```text
[TX #1/3] 0x11 0x03 0x00 0x00 ...
[RX #1/3] 0x11 0x03 0x12 0x00 ...
```

---

## 4.2. Parte 1: protocolo simplificado

A primeira parte utiliza um protocolo próprio e mais simples.

Não são utilizados:

* endereço MODBUS;
* CRC.

Cada mensagem possui um comando e a matrícula.

Os seis comandos são:

| Comando | Função                   |
| ------- | ------------------------ |
| `0xA1`  | Solicitar um inteiro     |
| `0xA2`  | Solicitar um número real |
| `0xA3`  | Solicitar uma string     |
| `0xB1`  | Enviar um inteiro        |
| `0xB2`  | Enviar um número real    |
| `0xB3`  | Enviar uma string        |

Exemplo:

```text
0xA1 + matrícula
```

A ESP32 responde com o valor solicitado.

Na Parte 1 não há retentativas. Caso a ESP32 não responda dentro do tempo definido, é apresentado:

```text
[ERRO] timeout
```

---

## 4.3. Parte 2: MODBUS modificado

A segunda parte utiliza uma estrutura baseada em MODBUS.

As mensagens possuem:

```text
Endereço
Função
Dados
Matrícula
CRC
```

A matrícula possui seis bytes e aparece imediatamente antes do CRC.

Os comandos utilizados são:

| Função | Subcódigo | Operação          |
| ------ | --------- | ----------------- |
| `0x23` | `0xA1`    | Solicitar inteiro |
| `0x23` | `0xA2`    | Solicitar float   |
| `0x23` | `0xA3`    | Solicitar string  |
| `0x16` | `0xB1`    | Enviar inteiro    |
| `0x16` | `0xB2`    | Enviar float      |
| `0x16` | `0xB3`    | Enviar string     |

O CRC é calculado sobre a mensagem antes do próprio CRC e é enviado em **little-endian**.

---

## 4.4. Parte 3: MODBUS do simulador

A terceira parte utiliza MODBUS para acessar os dispositivos do simulador de elevadores.

São utilizados os dispositivos:

| Dispositivo           | Endereço |
| --------------------- | -------: |
| Cabine 1              |   `0x11` |
| Cabine 2              |   `0x12` |
| Cabine 3              |   `0x13` |
| Controlador do prédio |   `0x20` |

São utilizadas principalmente as funções:

```text
0x03 - leitura de registradores
0x10 - escrita de registradores
```

### Cabines

As cabines possuem registradores relacionados a informações como:

* andar atual;
* cabine nivelada;
* estado da porta;
* comando da porta;
* corrente;
* carga;
* número de passageiros;
* posição;
* falhas.

### Controlador do prédio

O controlador possui informações relacionadas ao estado do prédio e à fila de chamadas.

Também é possível:

* ler o estado do prédio;
* ler chamadas;
* atribuir uma chamada a uma cabine;
* remover uma chamada;
* alterar a condição de contorno;
* comandar a porta da cabine.

Além das operações normais, existem operações de leitura e escrita brutas utilizadas para demonstrar as exceções previstas no trabalho.

---

## 4.5. Timeout e retentativas

As comunicações MODBUS possuem tratamento de erro.

O programa verifica:

* timeout;
* CRC;
* endereço;
* função;
* tamanho da resposta;
* quantidade de bytes;
* resposta de exceção.

Em caso de **timeout ou CRC inválido**, a comunicação pode ser repetida até o número configurado de tentativas.

Por padrão:

```text
Timeout: 300 ms
Tentativas: 3
```

Por exemplo:

```text
[TX #1/3] ...
[ERRO] timeout

[TX #2/3] ...
[ERRO] timeout

[TX #3/3] ...
[ERRO] timeout

[ERRO] falhou após 3 tentativas
```

Outros erros, como endereço ou função incorreta, são apenas informados ao usuário e não geram uma nova tentativa.

A Parte 1 não possui retentativas.

---

## 4.6. Tratamento de SIGINT

O tratamento de `Ctrl+C` da Entrega 1 continua funcionando na Entrega 2.

Ao pressionar:

```text
Ctrl+C
```

o programa:

1. interrompe o movimento da cabine;
2. interrompe uma possível espera pela UART;
3. fecha a porta serial;
4. zera o PWM;
5. aplica o freio;
6. libera os recursos utilizados.

Dessa forma, o programa pode ser encerrado com segurança mesmo durante uma comunicação com a ESP32.

---

# 5. Requisitos

| #  | Requisito                     | Implementação              |
| -- | ----------------------------- | -------------------------- |
| 1  | Utilizar C, C++ ou Rust       | C++17                      |
| 2  | UART 115200 8N1               | `uart/`                    |
| 3  | Porta UART configurável       | `config.ini`               |
| 4  | Matrícula com 6 dígitos       | `common/matricula.hpp`     |
| 5  | Matrícula em bytes, não ASCII | `common/matricula.hpp`     |
| 6  | CRC-16                        | `modbus/crc16.*`           |
| 7  | Protocolo simplificado        | `protocolo_simplificado.*` |
| 8  | MODBUS modificado             | `protocolo_modbus.*`       |
| 9  | MODBUS do simulador           | `elevador_modbus.*`        |
| 10 | Função `0x03`                 | `ElevadorModbus`           |
| 11 | Função `0x10`                 | `ElevadorModbus`           |
| 12 | Validação das respostas       | `RtuClient`                |
| 13 | Timeout                       | `PortaUart` / `RtuClient`  |
| 14 | Retentativas                  | `RtuClient`                |
| 15 | Leitura contínua              | módulo `elevador` / CLI    |
| 16 | Exceções MODBUS               | `RtuClient`                |
| 17 | Interface de terminal         | `cli/`                     |
| 18 | Impressão dos bytes TX/RX     | `cli/` e `TraceFn`         |
| 19 | Tratamento de `SIGINT`        | `shutdown.*` e `main.cpp`  |
| 20 | Makefile                      | `Makefile`                 |

---

# 6. Testes de funcionamento

Foram realizados testes das três partes.

### Parte 1

Foram testados os seis comandos:

```text
0xA1
0xA2
0xA3
0xB1
0xB2
0xB3
```

Foi verificado se a resposta recebida corresponde ao valor esperado.

### Parte 2

Foram testados os seis comandos MODBUS modificados, verificando:

* matrícula;
* CRC;
* bytes enviados;
* bytes recebidos;
* respostas da ESP32.

### Parte 3

Foram testados:

* leitura das cabines;
* leitura do prédio;
* comando das portas;
* condição de contorno;
* fila de chamadas;
* atribuição de chamadas;
* remoção de chamadas;
* exceções MODBUS;
* leitura contínua.

Também foi testado o comportamento quando a ESP32 não responde, verificando as três tentativas configuradas.

---

# 7. Gravação

O vídeo de demonstração pode ser acessado em: 

[Entrega 2 - Embarcados 2026.2](https://youtu.be/xKa6xiLgxcw?si=CQwsHjhsdz-zc4EA)

A demonstração apresenta:

* os seis comandos da Parte 1;
* os seis comandos da Parte 2;
* comunicação com as cabines e o controlador do prédio;
* leitura e escrita de registradores;
* exceções MODBUS;
* leitura contínua.