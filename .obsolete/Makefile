# change to gcc if clang not installed
CC=clang
CCX=clang++
# include traciapi to fix config file path
CXXFLAGS= -std=c++17 -I. -g "-I${SUMO_HOME}/include" "-I${SUMO_HOME}/src" -DDEBUG
LINKFLAGS= "-L${SUMO_HOME}/bin" -lsumocpp -lzmq
BIN_DIR = bin
OBJ_DIR = .obj
SOURCES := $(shell find * -type d -name "experiment" -prune -o -type f \( -name "*.cpp" -o -name "*.c" \) -print) 
SRC_LST := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCES)))
OBJECTS := $(addprefix $(OBJ_DIR)/,$(SRC_LST))

all: clean main

clean:
	@echo "Cleaning"
	rm -rf $(OBJ_DIR)/*
	rm -f $(BIN_DIR)/main

main: $(BIN_DIR)/main

$(BIN_DIR)/main: $(OBJECTS)
	@echo "Linking"
	@mkdir -p $(@D)
	$(CCX) -g $^ $(LINKFLAGS) -o $@

# %.o: %.cpp
# 	$(CC) $(CXXFLAGS) -c $< -o $@

.SECONDEXPANSION:
$(OBJECTS): $(OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -MMD -MP $(CXXFLAGS) -c $< -o $@

$(OBJECTS): $(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CCX) -MMD -MP $(CXXFLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)