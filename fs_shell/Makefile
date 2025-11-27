CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CPPFLAGS ?= -I.

OBJS = fs.o blockdev.o shell.o
TARGET = fs_shell

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
