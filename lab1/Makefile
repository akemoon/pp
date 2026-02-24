CC = gcc
CFLAGS = -Wall -pthread
LDFLAGS = -lm
TARGET = app
LOG_DIR = log
SRC_DIR = src
SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/config.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) -o $(TARGET) $(SRCS) $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf $(TARGET) $(LOG_DIR) 

run: $(TARGET)
	./$(TARGET)