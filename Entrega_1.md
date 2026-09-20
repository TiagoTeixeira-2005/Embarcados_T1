# Entrega 1 — Módulo da GPIO: Controle de Uma Cabine de Elevador

Trabalho 1 - Entrega 1 da disciplina de **Fundamentos de Sistemas Embarcados (2026/2)**

## 1. Objetivos

Implementar um programa na **Raspberry Pi** para controlar **uma única cabine** do simulador de elevadores — a **Cabine 1** — desenvolvendo um **módulo de GPIO** que exercite as quatro modalidades de entrada e saída digital exigidas no trabalho:

- **Saída on/off** — pinos de direção do motor de tração (`DIR1`/`DIR2`);
- **Saída PWM** — potência do motor de tração (1 kHz, 0 a 100%);
- **Entrada on/off** — cortina de luz da porta (com *debounce*) e **Sensor de Andar**;
- **Entrada por interrupção** — encoder em quadratura (canais A e B).

O ensaio é feito sobre um **modelo reduzido de 3 andares** (andares 0, 1 e 2), que exibe a cabine, a posição medida pelo encoder, o estado da cortina e as falhas registradas pelo simulador.

## 2. Modelo Reduzido

Nesta primeira entrega o modelo físico do elevador está simplificado a somente a **Cabine 1** e com os **andares 0, 1 e 2** (posições 0, 3000 e 6000 mm no poço). A seguir está a lista dos pinos da GPIO (pinos BCM).

> ⚠️ **Os pinos da Cabine 1 mudaram.**

<center>
<b>Tabela 1</b> — Pinos GPIO da Cabine 1 (BCM) — ⚠️ **Atualizado**
</center>

<center>

| Sinal | Função | GPIO RPi (BCM) | Direção |
|:--|:--|:-:|:--:|
| `PWM`     | Potência do motor de tração | 13 | Saída (PWM) |
| `DIR1`    | Direção 1                   | 22 | Saída on/off |
| `DIR2`    | Direção 2                   | 23 | Saída on/off |
| `ENC_A`   | Encoder canal A             | 20 | Entrada (interrupção) |
| `ENC_B`   | Encoder canal B             | 21 | Entrada (interrupção) |
| `CORTINA` | Cortina de luz da porta     | 26 | Entrada on/off |
| `SENSOR_ANDAR` | Sensor de Andar (bandeirola) | 0 | Entrada on/off |

</center>

<center>
<b>Tabela 2</b> — Comando de direção do motor
</center>

<center>

| Ação      | DIR1 | DIR2 |
|:--|:-:|:-:|
| **Livre**  | 0 | 0 |
| **Subir**  | 1 | 0 |
| **Descer** | 0 | 1 |
| **Freio**  | 1 | 1 |

</center>

### 2.1 Motor de Tração — Saída PWM

O motor é acionado por **PWM a 1 kHz** (sugestão: `softPwm` da WiringPi, ou equivalente na linguagem escolhida), com intensidade de 0 a 100%. A partida deve ser feita com **rampa de aceleração**, e não em degrau.

> **Atrito estático.** O motor do elevador **não arranca com *duty cycle* abaixo de 10%** — o
> torque de arranque de um motor de tração real é maior que o de regime. Uma vez
> em movimento, qualquer *duty cycle* movimenta o motor.

### 2.2 Encoder em Quadratura — Entrada por Interrupção

A cabine possui um encoder de dois canais (A e B) em quadratura. A leitura **deve ser feita por interrupção** em ambos os canais — implementações por *polling* em laço **não serão aceitas** neste item.

- Resolução: **1000 pulsos por metro** de deslocamento, em contagem de quadratura (4×);
- Altura entre andares: **3,0 m** → **3000 pulsos por andar**;
- Bancada: andares 0, 1 e 2 → posições **0, 3000 e 6000 pulsos**.

O sentido é resolvido pela **ordem das bordas** entre os canais. O contador de posição deve ser um **inteiro de 32 bits com sinal**.

A **tolerância de nivelamento** é de **±10 mm** (±10 pulsos). O programa deve parar a cabine dentro dessa faixa no andar de destino.

### 2.3 Cortina de Luz — Entrada on/off

Sinal normalmente em **baixa**, ativado em **alta** enquanto houver obstrução na porta. Deve ser lido com ***debounce***: o simulador emite, de propósito, **transições rápidas (bounce) em cada borda** — um tratamento sem debounce contará obstruções fantasmas.

O programa deve **imprimir imediatamente** no terminal cada evento de obstrução e de liberação da cortina. O botão "Obstruir porta" do widget é o estímulo de teste.

### 2.4 Sensor de Andar — Entrada on/off

O **Sensor de Andar**, na **GPIO 11** fica em **alta enquanto a cabine está dentro da
bandeirola de um andar** — uma região do poço centrada na posição do andar — e em
**baixa entre andares**.

O programa deve **ler esse sinal e detectar as suas duas bordas**: a de entrada e
a de saída da bandeirola. A cada borda, registre a **contagem do encoder** naquele
instante; ao sair da bandeirola, informe o **centro estimado**, que é a **média
entre as duas bordas**, e o **erro** em relação à posição nominal do andar.

Duas propriedades da bandeirola tornam esse exercício necessário:

- Ela é **muito mais larga que a tolerância de nivelamento**: são dezenas de
  milímetros contra os ±10 mm exigidos. **Parar assim que o sensor sobe não deixa
  a cabine nivelada**;
- A largura é **diferente em cada andar**, então não há como deduzir o centro a
  partir de uma borda só — daí a média entre as duas.

> **Nesta entrega o Sensor de Andar não comanda a parada.** As posições dos
> andares são conhecidas (**0, 3000 e 6000 mm**) e quem fecha a malha é o
> encoder, conforme a Seção 2.2. O sensor serve para você **medir** a bandeirola
> e conferir o seu contador contra uma referência absoluta.

Na Entrega Final este pino deixa de ser acessório: o prédio passa a ter 6 andares
cujas posições **não** são múltiplos de 3000 mm e variam de uma cabine para
outra, e cada Servidor Distribuído terá de descobri-las por interrupção nas duas
bordas de cada bandeirola. O que você fizer aqui é exatamente o núcleo daquele
procedimento.

## 3. Funcionamento Esperado

O programa deve implementar, no mínimo:

1. Uma **interface de terminal** com comandos para:
   - Definir para qual andar comandar o elevador: o comando deve resultado no movimento do elevador da posição atual para o andar especificado utilizando como feedback a posição do encoder, parando no andar desejado com uma tolerância de ±10 mm;
   - Acionamento direto do motor com direção (livre|subir|descer|freio) e valor do *duty cycle*
   - Imprimir o estado atual incluindo: posição (pulsos e mm), andar estimado, nivelamento, duty cycle atual, estado da cortina e estado do Sensor de Andar;
2. **Rampa de aceleração** na partida e **rampa de desaceleração** na aproximação do andar (o PID completo é objeto da Entrega Final — aqui basta um controle simples, por exemplo proporcional ou por histerese com velocidade reduzida na aproximação);
3. **Impressão imediata** dos eventos de cortina (obstrução/liberação) e das chegadas aos andares;
4. **Medição das bandeirolas**: a cada borda do Sensor de Andar, imprimir a contagem do encoder; ao sair da bandeirola, imprimir o **centro estimado** (média das duas bordas) e o **erro** em relação à posição nominal do andar (Seção 2.4);
5. **Proteção de fim de curso da bancada**: o programa não deve comandar movimento além dos 6000 mm (andar 2) nem abaixo de 0 mm;
6. Ao receber **SIGINT** (Ctrl+C): zerar o PWM, colocar a direção em **freio** e liberar os recursos de GPIO.

## 4. Requisitos

1. O programa deve ser desenvolvido em **Python**, **C/C++** ou **Rust**;
2. O código deve ser dividido em, no mínimo, **dois níveis**: um **módulo de GPIO** (baixo nível: on/off, PWM, entradas e interrupções) e a **lógica de controle** da cabine (alto nível). Programas em arquivo único terão a nota de qualidade zerada;
3. As saídas on/off devem comandar `DIR1`/`DIR2` conforme a Tabela 2;
4. A saída PWM deve operar a **1 kHz** com duty de 0 a 100%;
5. A cortina deve ser lida com **debounce**, com impressão imediata dos eventos;
6. O encoder deve ser lido **por interrupção nos dois canais**, com resolução de sentido e contador de 32 bits;
7. O **Sensor de Andar** deve ser lido com detecção das **duas bordas** da bandeirola, reportando a contagem do encoder em cada uma e o centro estimado pela média (Seção 2.4);
8. A cabine deve parar nos andares 0, 1 e 2 **dentro de ±10 mm**, verificado pela contagem do encoder — e o widget deve mostrar a cabine nivelada;
9. Não pode haver laços que ocupem 100% de CPU (*busy-wait*);
10. O programa deve tratar **SIGINT** conforme o item 6 da Seção 3;
11. Fornecer **Makefile** (C/C++) ou **requirements.txt** (Python) e instruções de execução no README;
12. Entregar **vídeo curto** (até 5 min) demonstrando: comando `andar` para os três andares, a leitura do encoder acompanhando o widget, o teste da cortina com o botão "Obstruir porta" e a **medição de uma bandeirola** com as duas bordas e o centro estimado. Todos os alunos do grupo devem aparecer no vídeo com câmera aberta.

## 5. Critérios de Avaliação

<center>
<b>Tabela 3</b> — Critérios da Entrega 1 (total: 1,0 ponto do Trabalho 1)
</center>

<center>

| Item | Detalhe | Valor |
|:--|:--|:-:|
| Saídas digitais on/off | `DIR1`/`DIR2` comandando livre, subir, descer e freio | 0,15 |
| Saída PWM | 1 kHz, duty 0–100%, rampa de aceleração | 0,15 |
| Entrada on/off — cortina | Cortina com debounce e log imediato dos eventos | 0,2 |
| Entradas por interrupção — encoder | Encoder em quadratura nos dois canais, sentido e contagem de 32 bits | 0,2 |
| Entrada on/off — Sensor de Andar | Detecção das duas bordas da bandeirola, contagem do encoder em cada borda e centro estimado pela média | 0,1 |
| Controle de posição da Cabine 1 | Parada nos andares 0–2 dentro de ±10 mm, sem busy-wait, com SIGINT tratado | 0,2 |

</center>

## 6. Referências

### Encoders em Quadratura

- [Leitura do Encoder E2-Q2](https://www.robocore.net/tutoriais/leitura-encoder-e2q2?srsltid=AfmBOopwl-sKz-KsNO2NcQMFcifOiIsLjWvX7g-m2zY3fDtd7u5CLuKk)  
- [Undestanding Quadrature Encoder](https://www-fiveflute-com.translate.goog/guide/understanding-quadrature-encoders-a-comprehensive-guide-for-mechanical-engineers/?_x_tr_sl=en&_x_tr_tl=pt&_x_tr_hl=pt&_x_tr_pto=tc)  

### Bibliotecas Python

- [gpiozero](https://gpiozero.readthedocs.io)
- [RPi.GPIO](https://pypi.org/project/RPi.GPIO/) — [Exemplos](https://sourceforge.net/p/raspberry-gpio-python/wiki/Examples/)

### Bibliotecas C/C++

- [WiringPi](http://wiringpi.com/) — GPIO e `softPwm`
- [BCM2835](http://www.airspayce.com/mikem/bcm2835/)
- [PiGPIO](http://abyz.me.uk/rpi/pigpio/index.html)

### Lista de Exemplos

Há um compilado de exemplos de acesso à GPIO em várias linguagens de programação como C, C#, Ruby, Perl, Python, Java e Shell (https://elinux.org/RPi_GPIO_Code_Samples).
