# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g
LDFLAGS = 
LIBS = 

# Project configuration
TARGET = memdb
SRCDIR = .
OBJDIR = obj
SOURCES = main.c
OBJECTS = $(SOURCES:%.c=$(OBJDIR)/%.o)
HEADERS = main.h

# Default port for running
DEFAULT_PORT = 12049

# Create object directory
$(OBJDIR):
	@mkdir -p $(OBJDIR)

# Compile object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link executable
$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $(TARGET)

# Build everything
all: $(TARGET)

# Run the database server
run: $(TARGET)
	./$(TARGET) $(DEFAULT_PORT)

# Run with custom port
run-port: $(TARGET)
	@echo "Usage: make run-port PORT=<port_number>"
	@if [ -z "$(PORT)" ]; then \
		echo "No port specified, using default $(DEFAULT_PORT)"; \
		./$(TARGET) $(DEFAULT_PORT); \
	else \
		./$(TARGET) $(PORT); \
	fi

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: clean $(TARGET)

# Release build (optimized)
release: CFLAGS += -DNDEBUG -O3
release: clean $(TARGET)

# Clean build artifacts
clean:
	rm -rf $(OBJDIR) $(TARGET)

# Force rebuild
rebuild: clean all

# Check for memory leaks (requires valgrind)
valgrind: debug
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET) $(DEFAULT_PORT)

# Static analysis (requires cppcheck)
analyze:
	cppcheck --enable=all --std=c99 $(SOURCES)

# Show help
help:
	@echo "Available targets:"
	@echo "  all      - Build the project (default)"
	@echo "  run      - Build and run with default port ($(DEFAULT_PORT))"
	@echo "  run-port - Build and run with custom port (make run-port PORT=8080)"
	@echo "  debug    - Build debug version"
	@echo "  release  - Build optimized release version"
	@echo "  clean    - Remove build artifacts"
	@echo "  rebuild  - Clean and build"
	@echo "  valgrind - Run with memory leak detection"
	@echo "  analyze  - Run static code analysis"
	@echo "  help     - Show this help message"

# Declare phony targets
.PHONY: all run run-port debug release clean rebuild valgrind analyze help

# Default target
.DEFAULT_GOAL := all