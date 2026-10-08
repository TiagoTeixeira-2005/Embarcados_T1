CXX      = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17
LDFLAGS  = -lwiringPi

SRC_DIR = src
BIN     = elevador_cabine1

all: $(BIN)

$(BIN): $(SRC_DIR)/main.cpp $(SRC_DIR)/gpio.hpp
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/main.cpp -o $@ $(LDFLAGS)

clean:
	rm -f $(BIN)

run: all
	sudo ./$(BIN)

.PHONY: all clean run