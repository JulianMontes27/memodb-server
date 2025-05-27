# Define the C compiler
CC = gcc

# Define compiler flags
# -Wall: Enable all common warnings
# -Wextra: Enable extra warnings
# -std=c11: Use the C11 standard
# -g: Include debugging information
CFLAGS = -Wall -Wextra -std=c11 -g

# Define linker flags (if any libraries are needed, add them here, e.g., -lm for math)
LDFLAGS =

# Define the executable name
TARGET = memodb.exe

# Define source files
SRCS = main.c

# Define object files (derived from source files)
OBJS = $(SRCS:.c=.o)

# Default target: builds the executable
all: $(TARGET)

# Rule to link object files into the executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Rule to compile .c files into .o files
# $<: The first prerequisite (the .c file)
# $@: The target (the .o file)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Phony targets - do not represent actual files
.PHONY: all clean run

# Clean target: removes object files and the executable
clean:
	-del $(OBJS) $(TARGET) 2>NUL

# Run target: executes the compiled program
run: $(TARGET)
	./$(TARGET)