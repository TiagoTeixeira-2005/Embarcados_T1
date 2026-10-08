# Entrega 2 — Comunicação UART (Protocolo Simplificado + MODBUS Modificado)

Trabalho 1 — Entrega 2 da disciplina de **Fundamentos de Sistemas Embarcados (2026/1)**

## 1. Objetivos

Implementar, na **Raspberry Pi**, a comunicação serial UART com a **ESP32** em três partes:

- **Parte 1** — Protocolo **simplificado** (sem MODBUS, sem CRC), com 6 comandos de solicitação e envio de dados.
- **Parte 2** — Wrapper **MODBUS modificado**, baseado no [`uart_modbus.md`](./uart_modbus.md), com matrícula expandida para os **6 últimos dígitos** ao invés dos 4 originais.
- **Parte 3** — Acesso aos **dados reais do simulador de elevadores** pelas funções MODBUS **`0x03` (Read Holding Registers)** e **`0x10` (Write Multiple Registers)**, sobre os mapas de registradores das Seções 5.2 e 5.3 do [enunciado do trabalho](./README.md).

O dispositivo conectado à UART responde aos três protocolos **simultaneamente** na mesma UART. 

A visualização dos comandos recebidos pelo dispositivo pode ser feita pelo dashboard **uart** no ThingsBoard que mostra cada comando em tempo real, identifica o protocolo de origem e destaca a matrícula.

![UART](./imagens/Dashboard_UART.png)

## 2. Configuração da UART

| Parâmetro            | Valor    |
|----------------------|----------|
| Baudrate             | 115200   |
| Paridade             | Nenhuma  |
| Bits de dados        | 8        |
| Bits de parada       | 1        |
| Controle de fluxo    | Nenhum   |

A porta UART utilizada na Raspberry Pi para comunicação com a ESP32 é definida no laboratório (geralmente `/dev/ttyS0` ou `/dev/serial0`).

> **Atenção:** o byte de matrícula é o valor **inteiro** do dígito (0–9), e **não** o caractere ASCII correspondente. Ou seja, o dígito `6` é transmitido como o byte `0x06`, não como `0x36`.

# Parte 1 — Protocolo Simplificado

Protocolo enxuto, sem endereçamento de dispositivo e sem CRC. Cada pacote começa com o byte de **comando** seguido do payload e da **matrícula de 6 dígitos** (6 últimos dígitos). O dispositivo responde imediatamente com bytes brutos no formato definido para cada comando.

## 1.1 Comandos de solicitação de informações

Pacote enviado pela RPi → Dispositivo:

```
[CMD][D1][D2][D3][D4][D5][D6]
```

onde `[D1]..[D6]` são os 6 últimos dígitos da matrícula em bytes brutos (0–9).

Exemplo para a matrícula `654321`: `0xA1 0x06 0x05 0x04 0x03 0x02 0x01` (7 bytes).

| Comando | Significado                  | Resposta do dispositivo                                          |
|:-------:|------------------------------|------------------------------------------------------------|
| `0xA1`  | Solicita inteiro             | 4 bytes (`int32_t` little-endian) — **valor constante**    |
| `0xA2`  | Solicita float               | 4 bytes (`float` little-endian) — **valor constante**      |
| `0xA3`  | Solicita string              | 1 byte de tamanho (`N`) seguido de `N` bytes — **string constante** |

## 1.2 Comandos de envio de informações

Pacote enviado pela RPi → Dispositivo:

| Comando | Estrutura do pacote                                          | Tamanho        |
|:-------:|--------------------------------------------------------------|:--------------:|
| `0xB1`  | `[0xB1][int32 LE: 4 bytes][6 dígitos da matrícula]`          | 11 bytes       |
| `0xB2`  | `[0xB2][float LE: 4 bytes][6 dígitos da matrícula]`          | 11 bytes       |
| `0xB3`  | `[0xB3][N: 1 byte][string: N bytes][6 dígitos da matrícula]` | `8 + N` bytes  |

Resposta do dispositivo:

| Comando | Resposta                                                                  |
|:-------:|---------------------------------------------------------------------------|
| `0xB1`  | 4 bytes: `int_recebido * último_dígito_da_matrícula`                      |
| `0xB2`  | 4 bytes: `float_recebido * último_dígito_da_matrícula`                    |
| `0xB3`  | 1 byte de tamanho seguido da string `"Resposta da UART: " + string_enviada` |

> O **último dígito** da matrícula é o **menos significativo** (no exemplo `654321`, é `1`).

## 1.3 Erros de sintaxe

Se o dispositivo receber um byte de comando fora da faixa válida, ela publica um evento de erro no widget e descarta o pacote. A RPi deve detectar timeout ao não receber a resposta esperada e logar o erro.

# Parte 2 — Wrapper MODBUS Modificado

A Parte 2 segue a especificação completa do [`uart_modbus.md`](./uart_modbus.md), com **uma única diferença**:

> **A matrícula utilizada neste trabalho deve ser de 6 dígitos** (os 6 últimos da matrícula do aluno) em vez dos 4 dígitos originais. Todos os pacotes que incluíam matrícula no MODBUS agora têm **2 bytes a mais** entre o payload e o CRC-16.

Todo o restante (endereçamento, código de função, CRC-16, fluxo cliente/servidor) permanece **idêntico** ao descrito em `uart_modbus.md`.

## 2.1 Formato genérico atualizado

| *A.* Endereço | *B.* Função | *C.* Sub-código + dados | *D.* Matrícula | *E.* CRC-16 |
|:-:|:-:|:-:|:-:|:-:|
| 1 byte | 1 byte | n bytes | **6 bytes** | 2 bytes |

## 2.2 Lista de códigos (mesma da Tabela 1 de `uart_modbus.md`)

| Código | Sub-código | Solicitação                          | Mensagem de Retorno                                  |
|:------:|:----------:|--------------------------------------|------------------------------------------------------|
| `0x23` | `0xA1`     | Solicita inteiro (`int32`)           | `int` (4 bytes)                                      |
| `0x23` | `0xA2`     | Solicita float                       | `float` (4 bytes)                                    |
| `0x23` | `0xA3`     | Solicita string                      | `[len: 1 byte][string: len bytes]`                   |
| `0x16` | `0xB1`     | Envia inteiro                        | `int` (4 bytes)                                      |
| `0x16` | `0xB2`     | Envia float                          | `float` (4 bytes)                                    |
| `0x16` | `0xB3`     | Envia string                         | `[len: 1 byte][string: len bytes]`                   |

## 2.3 Exemplos de pacotes (matrícula `654321`)

**Solicita inteiro** (`0x01 0x23 0xA1 + matrícula + CRC`)

```
0x01 0x23 0xA1 0x06 0x05 0x04 0x03 0x02 0x01 [CRC_LO] [CRC_HI]
                ^─────── 6 dígitos ────────^
Total: 11 bytes (3 + 6 + 2)
```

**Envia inteiro `3245`** (`0x01 0x16 0xB1 + 4 bytes int + matrícula + CRC`)

```
0x01 0x16 0xB1 0xAD 0x0C 0x00 0x00 0x06 0x05 0x04 0x03 0x02 0x01 [CRC_LO] [CRC_HI]
                ^── int LE ──^   ^─────── 6 dígitos ────────^
Total: 15 bytes (3 + 4 + 6 + 2)
```

## 2.4 Requisitos da Parte 2

1. Reaproveitar a estrutura da Parte 1, adicionando o wrapper MODBUS (endereço, função, sub-código, CRC-16) em torno de cada comando.
2. **Calcular e validar o CRC-16** (mesmo polinômio do `uart_modbus.md` — código de referência em [`main/crc16.c`](../main/crc16.c) e equivalente Python em [`raspberry_pi_test_code/crc16.py`](../raspberry_pi_test_code/crc16.py)).
3. Tratar respostas com **bit de erro** (função com MSB setado) e o respectivo código de exceção.
4. Menu único na CLI deve permitir escolher **qual protocolo usar** (simplificado ou MODBUS) antes de cada comando, ou alternativamente disponibilizar dois sub-menus.
5. Imprimir em tela todos os bytes enviados/recebidos e os campos decodificados.

# Parte 3 — Leitura e Escrita dos Registradores do Simulador

Os dispositivos `0x01` das Partes 1 e 2 são didáticos: devolvem constantes. A Parte 3 conversa com os dispositivos **reais** do simulador — as três cabines e o Controlador do Prédio — e é a base do módulo MODBUS usado na Entrega Final pelos Servidores Distribuídos e pelo Servidor Central.

Os endereços, os mapas de registradores e o significado de cada campo estão nas **Seções 5.1, 5.2 e 5.3 do [enunciado do trabalho](./README.md)**. Esta Parte define apenas o **formato de trama** e as **funções** a implementar.

| Dispositivo | Endereço | Registradores (Leitura) | Registradores (Escrita) |
|:--|:-:|:-:|:--|
| Cabine 1 / 2 / 3 | `0x11` / `0x12` / `0x13` | offsets 0 a 8 | somente o 3 (`porta_comando`) |
| Controlador do Prédio | `0x20` | offsets 0 a 15 | 4, 5, 6, 9 e 10 |

Cada registrador tem **16 bits**. Os valores sem sinal são `uint16`; a exceção é o `posicao_mm` das cabines (offset 7), que é **`int16` com sinal**.

## 3.1 Formato da MENSAGEM

A matrícula de 6 dígitos continua obrigatória em toda **requisição**, sempre imediatamente antes do CRC. As **respostas** do simulador não incluem a matrícula.

**`0x03` — Read Holding Registers** (lê `qtd` registradores consecutivos a partir de `reg`)

```
Requisição: [addr][0x03][reg: 2][qtd: 2][matrícula: 6][CRC: 2]                 12 bytes
Resposta:   [addr][0x03][byte_count][valor_1: 2]...[valor_qtd: 2][CRC: 2]      5 + 2·qtd bytes
```

**`0x10` — Write Multiple Registers** (escreve `qtd` registradores consecutivos a partir de `reg`)

```
Requisição: [addr][0x10][reg: 2][qtd: 2][byte_count][valor_1: 2]...[valor_qtd: 2][matrícula: 6][CRC: 2]   13 + 2·qtd bytes
Resposta:   [addr][0x10][reg: 2][qtd: 2][CRC: 2]                                                          8 bytes
```

`byte_count` é sempre `2 · qtd`.

**Ordem dos bytes**:

| Campo | Ordem |
|:--|:-:|
| `reg` e `qtd` da **requisição** (`0x03` e `0x10`) | **little-endian** |
| valores enviados na **requisição `0x10`** | **little-endian** |
| valores devolvidos na **resposta `0x03`** | **big-endian** |
| eco de `reg` e `qtd` na **resposta `0x10`** | **big-endian** |
| CRC-16 (todas as mensagens) | little-endian |

## 3.2 Respostas de exceção

Quando a requisição é inválida, o simulador responde com a função com o **MSB setado** seguida do código de exceção:

```
[addr][função | 0x80][código][CRC: 2]      5 bytes
```

| Código | Significado | Quando ocorre |
|:-:|:--|:--|
| `0x01` | Função inválida | função diferente de `0x03` e `0x10` num dispositivo real |
| `0x02` | Endereço inválido | faixa fora do mapa; `qtd = 0`; escrita em registrador somente-leitura |
| `0x03` | Valor inválido | `porta_comando` fora de 0–2; `atribuicao_cabine` fora de 1–3 |

Mensagem com **CRC inválido** é descartada sem resposta — a RPi percebe por timeout.

## 3.3 Exemplos de MENSAGEM (matrícula `654321`)

**Lê os 9 registradores da Cabine 1** (`0x11`, offsets 0 a 8)

```
0x11 0x03 0x00 0x00 0x09 0x00 0x06 0x05 0x04 0x03 0x02 0x01 0x0E 0xE4
          ^─reg LE─^ ^─qtd LE─^ ^─────── 6 dígitos ────────^ ^─CRC─^
Resposta: 0x11 0x03 0x12 [andar_atual BE][nivelado BE] ... [falha BE] [CRC_LO] [CRC_HI]   (23 bytes)
```

**Escreve a Condição de Contorno no Prédio** (`0x20`, offsets 5 e 6: 25,3 °C → `253`; 1013 hPa)

```
0x20 0x10 0x05 0x00 0x02 0x00 0x04 0xFD 0x00 0xF5 0x03 0x06 0x05 0x04 0x03 0x02 0x01 0x56 0xC3
          ^─reg LE─^ ^─qtd LE─^  bc  ^─253 LE─^ ^1013 LE^ ^─────── 6 dígitos ────────^ ^─CRC─^
Resposta: 0x20 0x10 0x00 0x05 0x00 0x02 0x57 0x63
                    ^─reg BE─^ ^─qtd BE─^
```

**Exceção — faixa inválida na Cabine 1**

```
0x11 0x83 0x02 0xB0 0xF4
```

## 3.4 Requisitos da Parte 3

1. Reaproveitar o CRC-16 e as rotinas de envio/recepção da Parte 2.
2. **Validar a resposta** antes de usá-la: CRC, endereço e função iguais aos da requisição, `byte_count = 2·qtd` (`0x03`) e eco de `reg`/`qtd` idêntico ao enviado (`0x10`).
3. Timeout de **200 a 500 ms** e até **3 tentativas** em caso de timeout ou CRC inválido (Seção 5 do enunciado do trabalho). Respostas de **exceção não devem ser repetidas**: são reportadas com o código e o significado.
4. Menu da CLI com uma opção por função da Seção 3.4, incluindo um modo de **leitura contínua** que atualize o estado de uma cabine ou do prédio a cada segundo até o usuário interromper.
5. Demonstrar, na bancada, a cadeia completa:
   - após mais de 30 s sem escrita da Condição de Contorno, `le_estado_predio()` mostra `watchdog_ambiente = 1` e `barramento_max_ma = 3000`; após `escreve_condicao_contorno()`, o watchdog volta a `0` e o barramento sobe conforme o derating térmico (Seção 7.4 do enunciado do trabalho);
   - `comanda_porta()` abre e fecha a porta de uma cabine nivelada, e `le_estado_cabine()` acompanha a transição `fechada → abrindo → aberta`;
   - uma chamada registrada no dashboard aparece em `le_chamada_da_fila()`, é atribuída com `atribui_chamada()` e some da fila após `remove_chamada_da_fila()`;
   - uma escrita em registrador somente-leitura e uma faixa fora do mapa retornam as exceções `0x02` correspondentes.
6. Imprimir em tela todos os bytes enviados/recebidos e os campos decodificados, como nas Partes 1 e 2.

## 4. Requisitos Gerais

1. Linguagem: **Python**, **C/C++** ou **Rust**.
2. Cada operação deve imprimir em tela toda a sequência de bytes (envio e resposta) — **não vale apenas mostrar o valor decodificado**.
3. Tratamento adequado de erros: timeout, CRC inválido (Partes 2 e 3), comando desconhecido, byte de tamanho fora do range, respostas de exceção MODBUS (Parte 3).
4. Fornecer **Makefile** (C/C++) ou **requirements.txt** (Python) e instruções de execução no `README`.
5. O código deve ser estruturado em **funções independentes**, uma por comando, mais utilitários (`enviar_pacote`, `ler_resposta`, `calcular_crc`, etc.).
6. As três Partes devem coexistir no mesmo executável da Raspberry Pi (ou em executáveis irmãos com README explicando a estrutura).
7. O módulo da Parte 3 deve ser **importável** pelos programas da Entrega Final (Servidor Central e Servidores Distribuídos), separado da CLI.

## 5. Critérios de Avaliação

| Item                                                                  | Peso |
|-----------------------------------------------------------------------|:----:|
| Parte 1 — todos os 6 comandos funcionando corretamente                | 25 % |
| Parte 2 — todos os 6 comandos funcionando com CRC correto e matrícula de 6 dígitos | 35 % |
| Parte 3 — funções `0x03`/`0x10` sobre as cabines e o prédio, com ordem de bytes correta, validação da resposta, retentativas e tratamento de exceções | 40 % |

## 6. Entregáveis

- Commit no repositório com a Tag: `v2.0`.
- Vídeo curto (até 8 min) demonstrando a execução dos 12 comandos (6 + 6), a sequência de demonstração da Parte 3 (item 5 da Seção 3.5) e o widget do ThingsBoard recebendo os eventos em tempo real.
- README com instruções de build/execução e print da matrícula em destaque no widget.

## 7. Referências

- Especificação completa do MODBUS adaptado: [`uart_modbus.md`](./uart_modbus.md).
- Endereços e mapas de registradores das cabines e do prédio: Seções 5.1 a 5.3 do [enunciado do trabalho](./README.md).
- Código de CRC-16 de referência (igual ao usado pela ESP32): [`main/crc16.c`](../main/crc16.c).
