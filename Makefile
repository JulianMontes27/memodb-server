# Makefile for MemoDB event-driven server

CC = gcc
BASE_CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE
LDFLAGS = 
TARGET = memodb
SOURCES = memodb.c

# Debug flags
DEBUG_CFLAGS = $(BASE_CFLAGS) -g -DDEBUG -O0
# Release flags  
RELEASE_CFLAGS = $(BASE_CFLAGS) -O2 -DNDEBUG

.PHONY: all debug release clean test

# Default target
all: release

# Debug build
debug: 
	$(CC) $(DEBUG_CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

# Release build
release: 
	$(CC) $(RELEASE_CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

# Generic build target
$(TARGET): $(SOURCES) memodb.h
	$(CC) $(BASE_CFLAGS) -O2 -o $(TARGET) $(SOURCES) $(LDFLAGS)

# Clean build artifacts
clean:
	rm -f $(TARGET) *.o core

# Test the server
test: $(TARGET)
	@echo "Starting server in background..."
	./$(TARGET) 12049 &
	@echo "Server PID: $$!"
	@echo "Test with: telnet localhost 12049"
	@echo "Stop with: kill $$!"

# Install (optional)
install: release
	sudo cp $(TARGET) /usr/local/bin/

# Uninstall
uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)

# Show help
help:
	@echo "Available targets:"
	@echo "  all      - Build release version (default)"
	@echo "  debug    - Build debug version with symbols"
	@echo "  release  - Build optimized release version"
	@echo "  clean    - Remove build artifacts"
	@echo "  test     - Build and start server for testing"
	@echo "  install  - Install to /usr/local/bin"
	@echo "  help     - Show this help message"