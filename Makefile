all: server client

server: server.c common.h
	gcc -Wall server.c -o server

client: client.c common.h
	gcc -Wall client.c -o client

clean:
	rm -f server client