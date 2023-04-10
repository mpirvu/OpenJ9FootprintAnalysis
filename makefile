SOURCEDIR = src
OBJDIR = obj

# List of sources
SOURCES := $(wildcard $(SOURCEDIR)/*.cpp)

# List of objects
OBJECTS := $(SOURCES:$(SOURCEDIR)/%.cpp=$(OBJDIR)/%.o)

# List of dependencies
DEPS := $(OBJECTS:.o=.d)

# Compiler
CC = g++

# Compiler flags
CFLAGS = -std=c++17 -Wall -O2 -g

# Linker flags
LDFLAGS =

# Output
EXE = footprintAnalysis.linux

# Default rule
$(EXE): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

# Compile rule
$(OBJDIR)/%.o: $(SOURCEDIR)/%.cpp
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# Rule for generating dependencies
$(OBJDIR)%.d: $(SRCDIR)%.cpp
	@$(CC) $(CFLAGS)

# Clean rule
clean:
	rm -f $(EXE) $(OBJECTS) $(DEPS)

# Automatic dependency graph generation
CFLAGS += -MMD
-include $(DEPS)
