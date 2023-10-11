# change to gcc if clang not installed
CC=clang
CCX=clang++
# include traciapi to fix config file path
# gnu++ for pthread barriers
CXXFLAGS= -std=gnu++20 -I. -g -I./libs/traciapi "-I${SUMO_HOME}/include"
BIN_DIR = bin
OBJ_DIR = .obj
SOURCES := $(shell find * -type f -name "*.cpp" -o -name "*.c")
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
	$(CCX) -g $^ -o $@

# %.o: %.cpp
# 	$(CC) $(CXXFLAGS) -c $< -o $@

.SECONDEXPANSION:
$(OBJECTS): $(OBJ_DIR)/%.o: $$(wildcard %.c*)
	@mkdir -p $(@D)
ifeq "$(suffix $<)" ".cpp"
	$(CCX) -MMD -MP $(CXXFLAGS) -c $< -o $@ 
else
#equal for now
	$(CCX) -MMD -MP $(CXXFLAGS) -c $< -o $@
endif

-include $(OBJECTS:.o=.d)