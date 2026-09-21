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

O projeto foi organizado em dois arquivos, mantendo os dois níveis de abstração exigidos pelo enunciado:
```
src/

├── gpio.hpp
│   └── Controle dos GPIOs, PWM e interrupções
│
└── main.cpp
    └── Lógica da cabine e interface de terminal
```

O arquivo `gpio.hpp` contém a classe `GpioModule`, responsável pelo controle dos pinos, PWM e interrupções. Já o arquivo `main.cpp` concentra a lógica da cabine, incluindo o controle dos andares, a rampa de movimento, o tratamento da bandeirola, a proteção de fim de curso e a interface de terminal.

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

### 3.1 Interface de terminal

O programa possui alguns comandos para controlar e acompanhar a cabine localizados em `main.cpp`.

| Sub-item do enunciado | Comando da CLI | Onde está implementado |
|---|---|---|
| Definir andar de destino | `andar <0\|1\|2>` | `cabine_andar()` |
| Controlar o motor manualmente | `motor <livre\|subir\|descer\|freio> <duty 0-100> [segundos]` | `cabine_motor()`, `cabine_livre()`, `cabine_freio()` |
| Mostrar o estado da cabine | `estado` | `cabine_status()` |

O comando `andar` movimenta a cabine até o andar informado, utilizando o encoder para acompanhar a posição e determinar quando a cabine chegou ao destino dentro da tolerância definida.

Os comandos `motor subir` e `motor descer` permitem controlar o motor manualmente e podem receber uma duração opcional. Quando a duração é informada, o movimento é encerrado automaticamente após o tempo definido. Quando ela não é informada, a cabine continua em movimento até que o usuário utilize `motor freio`, pressione `Ctrl+C` ou até que um dos limites de fim de curso seja atingido.

Durante os movimentos, o programa utiliza `poll()` para verificar a entrada do terminal. Dessa forma, os comandos `estado` e `freio` continuam disponíveis enquanto a cabine está em movimento. O comando `estado` apresenta informações como posição, andar estimado, nivelamento, duty cycle, estado da cortina e sensor de andar.

Exemplo:

```
---- Estado da Cabine 1 ----
  Posicao          : 2990 pulsos (2990 mm)
  Andar estimado   : 1 (dist=10 pulsos)
  Nivelado (+-10mm): sim
  Duty atual       : 0.0%
  Cortina          : livre
  Sensor de Andar  : fora da bandeirola
-----------------------------
```

### 3.2 Rampa de aceleração e desaceleração

A rampa de aceleração e desaceleração é utilizada para evitar que o motor passe diretamente de parado para a velocidade definida. Essa lógica está implementada nas funções `cabine_andar()` e `cabine_motor()`, em `main.cpp`.

Ao iniciar um movimento, o duty aumenta gradualmente em passos definidos por `PASSO_RAMPA`, atualmente configurado para 2% a cada ciclo de controle de 20 ms. Durante esse processo, o valor mínimo definido por `DUTY_LENTO` garante um nível de potência suficiente para ajudar o motor a vencer o atrito inicial.

No comando `andar`, quando a cabine entra na região próxima ao destino, definida por `ZONA_LENTA`, o duty passa a ser reduzido gradualmente conforme a distância até o andar diminui. Nos comandos manuais com duração definida, a mesma lógica é utilizada para realizar a desaceleração antes da parada do motor.

### 3.3 Mensagens de eventos

O programa apresenta no terminal mensagens relacionadas a eventos importantes durante a execução.

No caso da cortina da porta, quando uma obstrução é detectada ou liberada após o tratamento de debounce, são exibidas as seguintes mensagens:
```
[CORTINA] Obstrucao detectada
[CORTINA] Porta liberada
```
Durante o deslocamento automático, quando a cabine entra na tolerância definida para o andar de destino, é exibida uma mensagem informando a chegada:
```
[CABINE] Chegou ao andar N (posicao=... erro=...)
```

Durante um movimento manual, o programa também identifica quando a cabine passa pela posição correspondente a um dos andares, mesmo que não exista um andar de destino definido:
```
[CABINE] Passou pelo andar N durante o movimento manual (posicao=... pulsos, nivelado)
```

### 3.4 Sensor de Andar

O sensor de andar é utilizado para identificar a passagem da cabine pela bandeirola de cada andar. O tratamento é realizado por `GpioModule::on_sensor_andar_edge()` e `ao_mudar_sensor_andar()`.

O programa considera tanto a entrada quanto a saída da bandeirola. Em cada uma dessas bordas, a posição atual fornecida pelo encoder é registrada. Quando a cabine sai da bandeirola, as duas posições são utilizadas para estimar o centro da faixa:
```
centro = (posição de entrada + posição de saída) / 2
```
O centro estimado é então comparado com a posição esperada do andar mais próximo, permitindo verificar o nivelamento da cabine.

Exemplo:
```
[SENSOR_ANDAR] Entrada na bandeirola em 2880 pulsos

[SENSOR_ANDAR] Saida da bandeirola em 3121 pulsos | centro estimado = 3000 | andar de referencia = 3000 mm | erro = 0 mm
```

### 3.5 Proteção de fim de curso

A proteção de fim de curso impede que a cabine ultrapasse os limites físicos definidos para a bancada. Essa verificação é realizada pela função `fim_de_curso()`, chamada durante os movimentos automáticos e manuais.

Os limites utilizados pelo programa são:
```
POS_MIN_PULSOS = 0
POS_MAX_PULSOS = 6000
```

A cada ciclo de controle, a posição atual da cabine é comparada com esses limites. Caso o movimento possa fazer a cabine ultrapassar um deles, o motor é parado e uma mensagem de segurança é exibida no terminal:
```
[CABINE] Fim de curso atingido - motor parado por seguranca
```

Essa proteção funciona tanto durante o comando `andar` quanto durante os comandos `motor subir` e `motor descer`.

### 3.6 Tratamento de SIGINT

O `main.cpp` também trata o sinal `SIGINT`, gerado quando o usuário pressiona `Ctrl+C`. O handler `tratar_sigint()` altera as flags utilizadas pelo programa para interromper o movimento atual e encerrar o laço principal da interface de terminal.

Após a interrupção, o programa executa o processo de encerramento chamando `g_gpio.cleanup()`. Essa função zera o duty do motor e aplica o freio antes de liberar os recursos utilizados pela GPIO.

Dessa forma, o programa encerra de maneira controlada e evita que o motor permaneça em funcionamento após o término da execução.

---

## 4. Requisitos

| # | Requisito | Onde foi implementado |
|---|---|---|
| 1 | Linguagem C, C++, Python ou Rust | O projeto foi desenvolvido em **C++17** |
| 2 | Utilizar pelo menos dois níveis de abstração | `GpioModule` controla os GPIOs e `main.cpp` concentra a lógica da cabine |
| 3 | Controlar DIR1 e DIR2 conforme a tabela do enunciado | `GpioModule::set_direction()` |
| 4 | PWM de 1 kHz com duty de 0 a 100% | `GpioModule::set_duty()`, usando `softPwmCreate` e `softPwmWrite` |
| 5 | Cortina com debounce e impressão do evento | `GpioModule::on_cortina_edge()` e `ao_mudar_cortina()` |
| 6 | Encoder nos dois canais usando interrupções | `wiringPiISR()` e `GpioModule::on_encoder_edge()` |
| 7 | Sensor de andar usando as duas bordas e cálculo do centro | `GpioModule::on_sensor_andar_edge()` e `ao_mudar_sensor_andar()` |
| 8 | Parada nos três andares com tolerância de ±10 mm | `cabine_andar()` |
| 9 | Não utilizar busy-wait | Os laços de movimento utilizam `sleep` ou `poll()` e as interrupções são gerenciadas pela WiringPi |
| 10 | Tratamento de `SIGINT` | `main.cpp` (`tratar_sigint()`) |
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

5. Testar o controle manual do motor com os comandos `motor subir 20`, `motor descer 20` e `motor freio 0`, verificando o funcionamento da rampa de aceleração e desaceleração. Durante o movimento, testar também os comandos `estado` e `freio`.

6. Deixar o comando `motor descer 20` em execução, sem duração definida, próximo ao andar 0 e verificar se a proteção de fim de curso é acionada automaticamente, exibindo a mensagem `[CABINE] Fim de curso atingido...`.

7. Executar `top` em outra sessão SSH para verificar o uso de CPU do programa durante a execução.

8. Pressionar `Ctrl+C` para verificar o encerramento correto do programa, incluindo a parada do motor e a liberação dos recursos utilizados.
