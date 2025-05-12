CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -pthread

all: proxy_server

proxy_server: proxy_server.c proxy_parse.o
	$(CC) $(CFLAGS) -o proxy_server proxy_server.c proxy_parse.o $(LDFLAGS)

proxy_parse.o: proxy_parse.c proxy_parse.h
	$(CC) $(CFLAGS) -c proxy_parse.c

clean:
	rm -f proxy_server *.o

.PHONY: all clean
