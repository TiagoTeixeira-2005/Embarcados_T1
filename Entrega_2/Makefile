# Makefile - Trabalho 1 (Entrega 1 + Entrega 2), C++17.
#
#   make          compila o executavel ./elevador_cabine1 (GPIO + UART/MODBUS)
#   make lib      compila apenas a biblioteca de comunicacao libelevador.a
#   make clean    remove tudo o que foi gerado
#
# Todos os caminhos sao relativos a esta pasta. A unica dependencia externa e a
# WiringPi (somente para o executavel da cabine); a biblioteca usa apenas a
# libc/libstdc++.

CXX      ?= g++
AR       ?= ar
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

LIBS_GPIO ?= -lwiringPi -lpthread

ALL_CPPFLAGS = -Isrc $(CPPFLAGS) -MMD -MP

TARGET = elevador_cabine1
LIB    = libelevador.a
BUILD  = build

SRC_LIB  = $(wildcard src/common/*.cpp src/uart/*.cpp src/modbus/*.cpp src/elevador/*.cpp)
SRC_COM  = $(wildcard src/config/*.cpp src/protocolo/*.cpp)
SRC_CLI  = $(wildcard src/cli/*.cpp)
SRC_MAIN = src/main.cpp

OBJ_LIB  = $(SRC_LIB:%.cpp=$(BUILD)/%.o)
OBJ_COM  = $(SRC_COM:%.cpp=$(BUILD)/%.o)
OBJ_CLI  = $(SRC_CLI:%.cpp=$(BUILD)/%.o)
OBJ_MAIN = $(SRC_MAIN:%.cpp=$(BUILD)/%.o)

.PHONY: all lib clean

all: $(TARGET)

lib: $(LIB)

$(TARGET): $(OBJ_MAIN) $(OBJ_CLI) $(OBJ_COM) $(LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS_GPIO)

$(LIB): $(OBJ_LIB)
	$(AR) rcs $@ $^

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(ALL_CPPFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) $(TARGET) $(LIB)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)