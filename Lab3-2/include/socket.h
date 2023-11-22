#ifndef __SOCKET_H__
#define __SOCKET_H__
#include <winsock2.h>
#include <cstdio>

#define CLIENT_PORT htons(8001)
#define CLIENT_IP_ADDRESS "127.0.0.1"
#define SERVER_PORT htons(8000)
#define SERVER_IP_ADDRESS "127.0.0.1"

WSADATA wsaData;
SOCKET serverSock;
SOCKET clientSock;
SOCKADDR_IN serverAddr;
SOCKADDR_IN clientAddr;
int sockaddrSize = sizeof(sockaddr_in);

void error(const char *msg)
{
	printf("%s failed with error %d", msg, WSAGetLastError());
	closesocket(clientSock);
	WSACleanup();
	exit(1);
}

void prepareSocket(bool isClient = 1)
{
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		error("WSAStartup");
	}

	clientAddr.sin_family = AF_INET;
	clientAddr.sin_port = CLIENT_PORT;
	clientAddr.sin_addr.s_addr = inet_addr(CLIENT_IP_ADDRESS);
	
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = SERVER_PORT;
	serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP_ADDRESS);
	if (isClient)
	{
		clientSock = socket(AF_INET, SOCK_DGRAM, 0);
		if (clientSock == INVALID_SOCKET)
		{
			error("Socket creation");
		}
		if (bind(clientSock, (SOCKADDR *)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR)
		{
			error("Bind");
		}
	}
	else
	{
		serverSock = socket(AF_INET, SOCK_DGRAM, 0);
		if (serverSock == INVALID_SOCKET)
		{
			error("Socket creation");
		}
		if (bind(serverSock, (SOCKADDR *)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)
		{
			error(" Bind");
		}
	}
}
#endif