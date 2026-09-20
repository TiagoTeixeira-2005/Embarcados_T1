# Trabalho 1 — Entrega 1: Controle da Cabine 1

**Disciplina:** Fundamentos de Sistemas Embarcados (2026/2)
**Linguagem:** C++17  
**Biblioteca de GPIO:** WiringPi

Este README apresenta como o programa da Cabine 1 foi organizado, como executá-lo e onde cada requisito do trabalho foi implementado.

## Alunos
|Matrícula | Aluno |
| -- | -- |
|  Matricula 1  |  Nome 1  |
|  Matricula 2  |  Nome 2  |


## Sumário

- [1. Como compilar e executar](#1-como-compilar-e-executar)
- [2. Arquitetura e pinagem](#2-arquitetura-e-pinagem)
  - [2.1. Pinagem utilizada](#21-pinagem-utilizada)
- [3. Funcionamento do programa](#3-funcionamento-do-programa)
  - [3.1. Interface de terminal](#31-interface-de-terminal)
  - [3.2. Rampa de aceleração e desaceleração](#32-rampa-de-aceleração-e-desaceleração)
  - [3.3. Mensagens de eventos](#33-mensagens-de-eventos)
  - [3.4. Sensor de Andar](#34-sensor-de-andar)
  - [3.5. Proteção de fim de curso](#35-proteção-de-fim-de-curso)
  - [3.6. Tratamento de SIGINT](#36-tratamento-de-sigint)
- [4. Requisitos](#4-requisitos)
- [5. Gravação](#5-gravação)
---

## 1. Como compilar e executar

Para executar o programa, é necessário ter a WiringPi instalada na Raspberry Pi. Para verificar se ela está disponível, pode ser usado:

```bash
gpio -v
```

Depois, basta compilar e executar:

```bash
make  # compila (g++, C++17)
./elevador_cabine1
```

---

## 2. Arquitetura e pinagem

O projeto foi dividido em três partes principais:

```
src/

├── gpio.hpp / gpio.cpp
│   └── Controle dos GPIOs e PWM

├── cabine.hpp / cabine.cpp
│   └── Lógica de funcionamento da cabine

└── main.cpp
    └── Interface de terminal 
```

A ideia foi separar o controle dos componentes da GPIO da lógica da cabine. Assim, a classe Cabine utiliza o GpioDriver para acessar os pinos, enquanto o main.cpp fica responsável pela interface com o usuário.

### 2.1. Pinagem utilizada

| Sinal          | Função                        | GPIO (BCM) | Direção               |
|----------------|--------------------------------|:----------:|------------------------|
| PWM            | Potência do motor de tração    | 13         | Saída (PWM)            |
| DIR1           | Direção 1                      | 22         | Saída       |
| DIR2           | Direção 2                      | 23         | Saída       |
| ENC_A          | Encoder canal A                | 20         | Entrada (interrupção)  |
| ENC_B          | Encoder canal B                | 21         | Entrada (interrupção)  |
| CORTINA        | Cortina de luz da porta         | 26         | Entrada      |
| SENSOR_ANDAR   | Sensor de Andar (bandeirola)    | 0          | Entrada      |

---

## 3. Funcionamento do programa

### 3.1 Interface de terminal (`main.cpp`)

O programa possui alguns comandos para controlar e acompanhar a cabine.

| Sub-item do enunciado | Comando da CLI | Onde está implementado |
|---|---|---|
| Definir andar de destino | `andar <0\|1\|2>` | `Cabine::irParaAndar()` |
| Controlar o motor manualmente | `motor <livre\|subir\|descer\|freio> <duty 0-100>` | `Cabine::motorManual()` |
| Mostrar o estado da cabine | `estado` | `Cabine::imprimirEstado()`|

Quando é usado o comando `andar`, a cabine recebe a posição do andar como alvo e continua se movimentando até que o encoder indique que ela chegou dentro da tolerância definida.

O comando `estado` mostra informações como posição, andar estimado, direção, duty cycle, estado da cortina e sensor de andar.

Exemplo:

```
---- Estado da Cabine 1 ----
  Posicao        : 2990 pulsos (2990.0 mm)
  Andar estimado : 1 (dist=10 pulsos)
  Nivelado (+-10mm): sim
  Direcao atual  : livre
  Duty atual     : 0.0%
  Cortina        : livre
  Sensor de Andar: fora da bandeirola
  Modo automatico: inativo
-----------------------------
```

### 3.2 Rampa de aceleração e desaceleração

A rampa é controlada em `Cabine::loopControle()`, no arquivo `cabine.cpp`.

Ao iniciar o movimento, o duty não passa imediatamente para o valor máximo. Ele aumenta gradualmente em passos de `RAMPA_PASSO_DUTY`, que atualmente é de 2% a cada ciclo de controle de 20 ms.

Também existe um duty mínimo de `DUTY_ARRANQUE`, usado para ajudar o motor a vencer o atrito inicial.

Quando a cabine se aproxima do andar, entra em uma região de desaceleração definida por `ZONA_DESACELERACAO`. Nessa região, o duty é reduzido gradualmente conforme a cabine se aproxima do alvo.

### 3.3 Mensagens de eventos

Alguns eventos são mostrados no terminal assim que acontecem.

Para a cortina da porta:
```
[CORTINA] Obstrucao detectada
[CORTINA] Porta liberada
```
Essas mensagens só são exibidas depois que a mudança de estado passa pelo debounce.

Quando a cabine chega ao andar:
```
[CABINE] Chegou ao andar N (posicao=... erro=...)
```
A mensagem é exibida quando a posição entra na tolerância definida.

### 3.4 Sensor de Andar

O sensor de andar é tratado em `Cabine::onSensorAndarEdge()`.

São consideradas as duas bordas da bandeirola:

- entrada na bandeirola;
- saída da bandeirola.

Em cada borda, o programa registra a posição indicada pelo encoder.

Ao sair da bandeirola, essas duas posições são usadas para calcular o centro aproximado:
```
centro = (posição de entrada + posição de saída) / 2
```
Depois, esse valor é comparado com a posição esperada do andar.

Exemplo:
```
[SENSOR_ANDAR] Entrada na bandeirola em 2880 pulsos

[SENSOR_ANDAR] Saida da bandeirola em 3121 pulsos | centro estimado = 3000 | andar de referencia = 3000 mm | erro = 0 mm
```

### 3.5 Proteção de fim de curso

A proteção de fim de curso também é feita em `Cabine::loopControle()`.

A cada ciclo de controle, o programa verifica se a cabine está se aproximando de um dos limites da bancada:
```
POS_MIN_PULSOS = 0
POS_MAX_PULSOS = 6000
```
Se o movimento puder fazer a cabine ultrapassar um desses limites, o motor é parado e o programa mostra uma mensagem no terminal:
```
[CABINE] Fim de curso atingido...
```
Essa proteção funciona tanto durante o movimento automático quanto no controle manual do motor.

### 3.6 Tratamento de SIGINT

O `main.cpp` também trata o sinal `SIGINT`, gerado quando o usuário pressiona `Ctrl+C`.

O handler `tratarSigint()` apenas altera uma flag. Depois disso, o programa principal executa o processo de encerramento da cabine.

A função `Cabine::cleanup()`:

1. Para o motor usando `paradaEmergencia()`;
2. Zera o duty e aplica o freio;
3. Encerra as threads de controle e debounce;
4. Libera os recursos da GPIO;
5. Encerra as threads utilizadas pelo PWM.

Dessa forma, o programa não termina deixando o motor em funcionamento.

---

## 4. Requisitos

| # | Requisito | Onde foi implementado |
|---|---|---|
| 1 | Linguagem C, C++, Python ou Rust | O projeto foi desenvolvido em **C++17** |
| 2 | Utilizar pelo menos dois níveis de abstração | `GpioDriver` controla os GPIOs e `Cabine` concentra a lógica da cabine |
| 3 | Controlar DIR1 e DIR2 conforme a tabela do enunciado | `Cabine::aplicarDirecao()` |
| 4 | PWM de 1 kHz com duty de 0 a 100% | `GpioDriver::PwmChannel` |
| 5 | Cortina com debounce e impressão do evento | `Cabine::loopDebounceCortina()` |
| 6 | Encoder nos dois canais usando interrupções | `GpioDriver::inputInterruptSetup()` e `Cabine::onEncoderEdge()` |
| 7 | Sensor de andar usando as duas bordas e cálculo do centro | `Cabine::onSensorAndarEdge()` |
| 8 | Parada nos três andares com tolerância de ±10 mm | `Cabine::loopControle()` |
| 9 | Não utilizar busy-wait | As threads utilizam `sleep_for` ou `nanosleep`, e as interrupções são bloqueantes |
| 10 | Tratamento de `SIGINT` | `main.cpp` |
| 11 | Makefile e instruções de execução | `Makefile` e este README |
| 12 | Vídeo de demonstração de até 5 minutos | Tópico 5 |

---

## 5. Gravação

O vídeo de demonstração pode ser acessado em: [Link para o vídeo](URL_DO_VIDEO).

Para a gravação, foi seguida uma sequência de testes para verificar o funcionamento dos principais requisitos do projeto:

1. Executar `estado` para verificar o estado inicial da cabine.

2. Executar `andar 1`, `andar 2` e `andar 0`, verificando se a cabine chega a cada andar dentro da tolerância de ±10 mm e se a mensagem `[CABINE] Chegou ao andar N...` é exibida.

3. Durante os deslocamentos, verificar as mensagens `[SENSOR_ANDAR]`, observando os registros de entrada, saída, centro estimado e erro em relação à posição esperada do andar.

4. Acionar a opção **"Obstruir porta"** no widget e verificar o funcionamento do debounce, observando as mensagens de obstrução e liberação da cortina.

5. Testar o controle manual do motor com os comandos `motor subir 20`, `motor descer 20` e `motor freio 0`, verificando também o funcionamento da rampa de aceleração e desaceleração.

6. Deixar o comando `motor descer 20` em execução próximo ao andar 0 e verificar se a proteção de fim de curso é acionada automaticamente, exibindo a mensagem `[CABINE] Fim de curso atingido...`.

7. Executar `top` em outra sessão SSH para verificar o uso de CPU do programa durante a execução.

8. Pressionar `Ctrl+C` para verificar o encerramento correto do programa, incluindo a parada do motor e a liberação dos recursos utilizados.