# =============================================================================
# COMPLETE MAKEFILE FOR MEMODB PROJECT WITH TREE MODULE
# =============================================================================

# Define the C compiler to use
# CC is a standard make variable that specifies which compiler to invoke
CC = gcc

# Compiler flags - these control how the code is compiled
# -Wall: Enable all common warning messages (helps catch potential bugs)
# -Wextra: Enable extra warning messages beyond -Wall
# -std=c99: Use C99 standard (ensures consistent behavior across systems)
# -O2: Optimization level 2 (balances speed vs compilation time)
# -g: Include debugging information in the executable (for gdb, valgrind)
CFLAGS = -Wall -Wextra -std=c99 -O2 -g

# Linker flags - passed to the linker when creating the final executable
# Currently empty, but can include things like -L for library paths
LDFLAGS = 

# Libraries to link against
# Currently empty, but would include things like -lm for math library
LIBS = 

# =============================================================================
# PROJECT STRUCTURE CONFIGURATION
# =============================================================================

# Name of the final executable file
TARGET = memdb

# Source directory (where .c files are located)
# Using '.' means current directory
SRCDIR = .

# Object directory (where compiled .o files will be stored)
# Keeping object files separate keeps the project clean
OBJDIR = obj

# List all source files that need to be compiled
# Each .c file will be compiled into a corresponding .o file
SOURCES = memodb.c tree.c

# Generate list of object files from source files
# This substitution replaces .c with obj/%.o for each source file
# Example: memodb.c becomes obj/memodb.o, tree.c becomes obj/tree.o
OBJECTS = $(SOURCES:%.c=$(OBJDIR)/%.o)

# List all header files
# Make will check these for changes to determine if recompilation is needed
# NOW INCLUDES YOUR RENAMED FILES: memodb.h instead of main.h
HEADERS = memodb.h tree.h

# Default port number for running the database server
DEFAULT_PORT = 12049

# =============================================================================
# BUILD TARGETS FOR DIFFERENT SCENARIOS
# =============================================================================

# Build the memodb server (memodb.c only)
# Since both files have main(), we build them separately
$(TARGET): obj/memodb.o | $(OBJDIR)
	@echo "Building memodb server executable..."
	$(CC) $(LDFLAGS) obj/memodb.o $(LIBS) -o $(TARGET)
	@echo "✓ Memodb server built successfully!"

# Build just the tree module for testing
# This compiles tree.c into a standalone executable
tree-test: obj/tree.o | $(OBJDIR)
	@echo "Building standalone tree executable for testing..."
	$(CC) $(LDFLAGS) obj/tree.o $(LIBS) -o tree-test
	@echo "✓ Tree test executable created: ./tree-test"

# Build both executables
both: $(TARGET) tree-test
	@echo "✓ Both executables built:"
	@echo "  - memdb (database server)"
	@echo "  - tree-test (tree module test)"

# =============================================================================
# OBJECT FILE COMPILATION RULES
# =============================================================================

# Create object directory if it doesn't exist
# The @ symbol suppresses echoing the command to the terminal
# -p flag creates parent directories if needed and doesn't error if dir exists
$(OBJDIR):
	@mkdir -p $(OBJDIR)
	@echo "Created object directory: $(OBJDIR)"

# Pattern rule for compiling individual source files into object files
# This rule applies to both memodb.c and tree.c
# $< refers to the first prerequisite (the .c file)
# $@ refers to the target (the .o file)
# | $(OBJDIR) makes $(OBJDIR) an order-only prerequisite
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADERS) | $(OBJDIR)
	@echo "Compiling $< into $@"
	@echo "  Checking dependencies: $(HEADERS)"
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "  ✓ Compilation successful"

# =============================================================================
# CONVENIENCE TARGETS
# =============================================================================

# Default target - builds the memodb server (since both files have main())
# This is what runs when you just type 'make'
all: $(TARGET)
	@echo "=== MEMODB SERVER BUILD COMPLETE ==="
	@echo "Main executable: $(TARGET) (memodb server)"
	@echo "To also build tree test: make tree-test"
	@echo "To build both: make both"
	@echo "Ready to run: ./$(TARGET) [port]"

# Build and run the complete memodb server with default port
run: $(TARGET)
	@echo "Starting memdb server on port $(DEFAULT_PORT)"
	@echo "Server includes both memodb and tree functionality"
	./$(TARGET) $(DEFAULT_PORT)

# Build and run with a custom port
# Usage: make run-port PORT=8080
run-port: $(TARGET)
	@echo "Usage: make run-port PORT=<port_number>"
	@if [ -z "$(PORT)" ]; then \
		echo "No port specified, using default $(DEFAULT_PORT)"; \
		./$(TARGET) $(DEFAULT_PORT); \
	else \
		echo "Starting memdb server on port $(PORT)"; \
		./$(TARGET) $(PORT); \
	fi

# Build and run just the tree module for testing
run-tree: tree-test
	@echo "Running standalone tree test..."
	./tree-test

# =============================================================================
# BUILD VARIANTS
# =============================================================================

# Debug build - optimized for debugging
# Adds DEBUG preprocessor define and removes optimization
# Rebuilds everything from scratch with debug settings
debug: CFLAGS += -DDEBUG -O0 -ggdb3
debug: clean $(TARGET)
	@echo "=== DEBUG BUILD COMPLETE ==="
	@echo "Built with:"
	@echo "  - No optimization (-O0)"
	@echo "  - Full debugging symbols (-ggdb3)"
	@echo "  - DEBUG preprocessor flag defined"
	@echo "Ready for debugging with gdb or valgrind"

# Release build - optimized for performance
# Adds NDEBUG (disables assert statements) and maximum optimization
release: CFLAGS += -DNDEBUG -O3 -march=native
release: clean $(TARGET)
	@echo "=== RELEASE BUILD COMPLETE ==="
	@echo "Built with:"
	@echo "  - Maximum optimization (-O3)"
	@echo "  - Native CPU optimization (-march=native)"
	@echo "  - Assertions disabled (-DNDEBUG)"
	@echo "Optimized for production use"

# =============================================================================
# DEVELOPMENT AND TESTING TOOLS
# =============================================================================

# Run with Valgrind for memory leak detection
# Builds debug version first for better error reporting
valgrind: debug
	@echo "Running memdb with Valgrind memory checking..."
	@echo "This will detect memory leaks in both memodb and tree code"
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET) $(DEFAULT_PORT)

# Static code analysis using cppcheck
# Analyzes both memodb.c and tree.c for potential issues
analyze:
	@echo "Running static code analysis on all source files..."
	@echo "Analyzing: $(SOURCES)"
	cppcheck --enable=all --std=c99 --verbose $(SOURCES)
	@echo "Analysis complete"

# Test compilation with warnings as errors
# Ensures code compiles cleanly without any warnings
test-warnings: CFLAGS += -Werror
test-warnings: clean $(TARGET)
	@echo "✓ Code compiles without any warnings!"

# =============================================================================
# MAINTENANCE TARGETS
# =============================================================================

# Remove all build artifacts
# Cleans both the main executable and any test executables
clean:
	@echo "Cleaning all build artifacts..."
	rm -rf $(OBJDIR) $(TARGET) tree-test
	@echo "Removed:"
	@echo "  - Object directory: $(OBJDIR)"
	@echo "  - Memodb executable: $(TARGET)"
	@echo "  - Tree test executable: tree-test"
	@echo "Clean complete"

# Complete rebuild - clean everything then build fresh
rebuild: clean all
	@echo "Complete rebuild finished"

# =============================================================================
# HELP AND INFORMATION
# =============================================================================

# Display comprehensive help about this Makefile
help:
	@echo "=== MEMODB PROJECT MAKEFILE HELP ==="
	@echo ""
	@echo "MAIN TARGETS:"
	@echo "  make          - Build memodb server (memodb.c only)"
	@echo "  make tree-test- Build tree module test (tree.c only)"
	@echo "  make both     - Build both executables"
	@echo "  make run      - Build and run memodb server on port $(DEFAULT_PORT)"
	@echo "  make run-port PORT=8080 - Build and run memodb on custom port"
	@echo ""
	@echo "TESTING TARGETS:"
	@echo "  make run-tree - Build and run tree module standalone"
	@echo ""
	@echo "BUILD VARIANTS:"
	@echo "  make debug    - Build with debugging info (no optimization)"
	@echo "  make release  - Build optimized for production"
	@echo ""
	@echo "DEVELOPMENT TOOLS:"
	@echo "  make valgrind - Run with memory leak detection"
	@echo "  make analyze  - Run static code analysis"
	@echo "  make test-warnings - Test that code compiles without warnings"
	@echo ""
	@echo "MAINTENANCE:"
	@echo "  make clean    - Remove all build files"
	@echo "  make rebuild  - Clean and build from scratch"
	@echo "  make help     - Show this help"
	@echo "  make info     - Show current configuration"

# Show current project configuration
info:
	@echo "=== PROJECT CONFIGURATION ==="
	@echo "Target executable: $(TARGET)"
	@echo "Source files: $(SOURCES)"
	@echo "Header files: $(HEADERS)"
	@echo "Object files: $(OBJECTS)"
	@echo "Compiler: $(CC)"
	@echo "Compiler flags: $(CFLAGS)"
	@echo "Default port: $(DEFAULT_PORT)"
	@echo ""
	@echo "=== FILE STATUS ==="
	@echo "Checking if files exist..."
	@for file in $(SOURCES) $(HEADERS); do \
		if [ -f "$$file" ]; then \
			echo "  ✓ $$file exists"; \
		else \
			echo "  ✗ $$file missing"; \
		fi; \
	done

# =============================================================================
# SPECIAL MAKE CONFIGURATION
# =============================================================================

# Declare phony targets (targets that don't create files with the same name)
.PHONY: all run run-port run-tree tree-test both debug release valgrind analyze test-warnings clean rebuild help info

# Set the default target (what runs when you just type 'make')
.DEFAULT_GOAL := all

# =============================================================================
# USAGE EXAMPLES IN COMMENTS
# =============================================================================

# COMMON USAGE SCENARIOS:
#
# 1. Build everything together (normal development):
#    make
#    ./memdb 12049
#
# 2. Test just the tree module:
#    make tree-only
#    ./tree-test
#
# 3. Run the complete server:
#    make run
#
# 4. Debug memory issues:
#    make valgrind
#
# 5. Clean rebuild:
#    make rebuild