CC = gcc
CFLAGS = -Wall -g

all: server subscriber

server: server.c
	$(CC) $(CFLAGS) -o server server.c -lm

subscriber: subscriber.c
	$(CC) $(CFLAGS) -o subscriber subscriber.c

clean:
	rm -f server subscriber *.o *~
