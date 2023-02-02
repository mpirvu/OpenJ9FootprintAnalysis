CPP_FILES := $(wildcard src/*.cpp)
OBJ_FILES := $(addprefix obj/,$(notdir $(CPP_FILES:.cpp=.o)))
CC_FLAGS := -Wall -std=c++11 -O2 -g
LD_FLAGS :=
#DEBUG = 1
TARGET = footprintAnalysis.linux
$(TARGET): $(OBJ_FILES)
	g++ $(LD_FLAGS) -o $@ $^


obj/%.o: src/%.cpp
	g++ $(CC_FLAGS) -c -o $@ $<
all: $(TARGET)

clean:
	$(RM) $(TARGET) $(OBJ_FILES)

# Automatic dependency graph generation
CC_FLAGS += -MMD
-include $(OBJFILES:.o=.d)

