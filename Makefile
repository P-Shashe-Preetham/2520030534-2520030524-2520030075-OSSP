# ==============================================================================
# Linux System Information & Resource Monitoring Tool - Makefile
# ==============================================================================
# Course: Operating Systems and Systems Programming (25CS2104E)
#
# Compiler and Flags:
# - CC: GNU C Compiler (gcc)
# - CFLAGS:
#     -Wall -Wextra     : Enable comprehensive compiler warnings
#     -std=c11          : Use ISO C11 standard
#     -g                : Include debug symbols for GDB / Valgrind analysis
#     -Iinclude         : Include directory path for header files
#     -D_DEFAULT_SOURCE : Expose POSIX / BSD macro definitions (DT_DIR, dirent, etc.)
#     -D_POSIX_C_SOURCE=200809L : Expose POSIX.1-2008 API (sigaction, sem_timedwait)
# - LDFLAGS:
#     -pthread          : Link the POSIX Threads library
#     -lrt              : Link POSIX Realtime Extensions library (semaphores/timers)
# ==============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -Iinclude -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
LDFLAGS = -pthread -lrt

# Directory layout
SRC_DIR = src
OBJ_DIR = obj
INC_DIR = include

# Automatic source and object file discovery
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Output binary target name
TARGET = monitor

# Non-file targets
.PHONY: all clean test snapshot

# Default build target: compiles the full executable
all: $(TARGET)

# Link object files into the final monitor binary
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Rule to compile individual C source files into object (.o) files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Generate a one-shot mmap memory-mapped snapshot
snapshot: $(TARGET)
	./$(TARGET) --snapshot

# Clean binaries, object files, temporary logs, snapshots, and IPC pipes
clean:
	rm -rf $(OBJ_DIR) $(TARGET) logs/monitor.log logs/snapshot.dat /tmp/ossp_monitor.fifo

# Run the monitor
test: $(TARGET)
	./$(TARGET)
