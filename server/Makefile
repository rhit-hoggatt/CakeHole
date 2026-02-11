CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -g -pthread
LDFLAGS = -lldns -lpthread -lmicrohttpd -lcrypto
SANITIZE = -fsanitize=address
TARGET = server
SRC = server.c cacheSystem.c workQueue.c thread.c apiHandler.c hashmap.c cacheHandler.c runningAvgs.c dhcpServer.c dnssecHandler.c cJSON.c api_utils.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

debug: CFLAGS += -O0
debug: LDFLAGS += $(SANITIZE)
debug: $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)