# Makefile for MemoDB Server

# Define the C compiler
CC = gcc

# Define compiler flags:
# -Wall: Enable all warnings
# -Wextra: Enable extra warnings (more than -Wall)
# -g: Include debugging information
# -DDEBUG: Define DEBUG macro (for debug_log in main.c)
# -D_GNU_SOURCE: Define _GNU_SOURCE for GNU-specific extensions (now handled here)
# -std=c11: Use C11 standard
# -MMD -MP: Generate dependency files (.d) automatically
CFLAGS = -Wall -Wextra -g -DDEBUG -D_GNU_SOURCE -std=c11 -MMD -MP

# Define the name of the executable
TARGET = memodb_server

# Define all source files
SRCS = main.c tree.c

# Automatically determine object files from source files
OBJS = $(SRCS:.c=.o)

# Automatically determine dependency files from object files
DEPS = $(OBJS:.o=.d)

# Default target: builds the executable
.PHONY: all
all: $(TARGET)

# Rule to link object files into the executable
$(TARGET): $(OBJS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJS) -o $(TARGET) -pthread # -pthread for any potential threading needs
	@echo "Build successful: $(TARGET)"

# Rule to compile each C source file into an object file
# $<: The first prerequisite (the .c file)
# $@: The target (the .o file)
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Phony target for cleaning compiled files
.PHONY: clean
clean:
	@echo "Cleaning up..."
	$(RM) $(OBJS) $(DEPS) $(TARGET)
	@echo "Cleanup complete."

# Include automatically generated dependency files
# This ensures that if a header file changes, only the .c files
# that include it are recompiled.
-include $(DEPS)