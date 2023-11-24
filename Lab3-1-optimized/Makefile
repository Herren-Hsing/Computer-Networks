CC = g++
CFLAGS = -lws2_32

all: server client

server: server.cpp
	$(CC) server.cpp -o server $(CFLAGS)

client: client.cpp
	$(CC) client.cpp -o client $(CFLAGS)

clean:
	del server.exe client.exe routerLog.txt