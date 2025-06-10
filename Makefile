# Makefile for main.c with tree.c dependency
# Optimized for efficient compilation and proper dependency management

CC = gcc
BASE_CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE
LDFLAGS =

# Updated target and source files to match your requirements
TARGET = main
SOURCES = main.c tree.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = main.h tree.h

# Debug flags
DEBUG_CFLAGS = $(BASE_CFLAGS) -g -DDEBUG -O0
# Release flags  
RELEASE_CFLAGS = $(BASE_CFLAGS) -O2 -DNDEBUG

.PHONY: all debug release clean test help

# Default target
all: release

# CHANGE: Added object-based compilation for better efficiency
# This allows incremental compilation - only changed files are recompiled
debug: CFLAGS = $(DEBUG_CFLAGS)
debug: $(TARGET)

release: CFLAGS = $(RELEASE_CFLAGS)
release: $(TARGET)

# CHANGE: Main target now depends on object files, not source files
# This enables proper dependency tracking and incremental builds
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# CHANGE: Added specific dependency rules for precise header tracking
# This ensures only necessary files are recompiled when headers change
main.o: main.c main.h tree.h
	$(CC) $(CFLAGS) -c main.c -o main.o

tree.o: tree.c tree.h
	$(CC) $(CFLAGS) -c tree.c -o tree.o

# CHANGE: Enhanced clean target to remove object files
# Object files should be cleaned up to ensure fresh builds
clean:
	rm -f $(TARGET) $(OBJECTS) core

# CHANGE: Updated test target to use correct executable name
test: $(TARGET)
	@echo "Running $(TARGET)..."
	./$(TARGET)

# CHANGE: Install target now uses correct executable name
install: release
	sudo cp $(TARGET) /usr/local/bin/

# CHANGE: Uninstall target updated for correct executable name
uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)

# CHANGE: Updated help text to reflect new structure
help:
	@echo "Available targets:"
	@echo "  all      - Build release version (default)"
	@echo "  debug    - Build debug version with symbols"
	@echo "  release  - Build optimized release version"
	@echo "  clean    - Remove build artifacts ($(TARGET) and *.o)"
	@echo "  test     - Build and run $(TARGET)"
	@echo "  install  - Install to /usr/local/bin"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Source files: $(SOURCES)"
	@echo "Header files: $(HEADERS)"

# CHANGE: Added specific dependency information
# Dependencies based on actual #include relationships:
# main.o: main.c main.h tree.h (assuming main.c includes both headers)
# tree.o: tree.c tree.h (tree.c only includes tree.h)
# $(TARGET): main.o tree.o