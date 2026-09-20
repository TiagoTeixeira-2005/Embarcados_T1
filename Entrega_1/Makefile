CXX      = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++17 -pthread
LDFLAGS  = -lwiringPi -pthread

SRC_DIR = src
OBJ_DIR = build
BIN     = elevador_cabine1

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN)

run: all
	sudo ./$(BIN)

.PHONY: all clean run
