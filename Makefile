CC=gcc
CFLAGS=-I.

build : server.c client.c
	$(CC) server.c -o server
	$(CC) client.c -o client
	mkdir -p server_repo
	mkdir -p client_repo

server : server.c
	$(CC) server.c -o server

client : client.c
	$(CC) client.c -o client

clean :
	rm server client
