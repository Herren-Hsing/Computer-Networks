#include <Winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <time.h>

SOCKET sock_client;
char timestamp[20];

// 定义报文结构体
struct Message
{
	char type;		 // 标识消息类型
	char obj[5];	 // 私聊功能 目的客户端
	char content[1]; // 先使用长度为 1 的数组，实际长度会动态分配
};

void error(char *msg)
{
	printf("%s", msg);
	fprintf(stderr, " failed with error %d\n", WSAGetLastError());
	closesocket(sock_client);
	WSACleanup();
	exit(1);
}

void print_time()
{
	time_t rawtime;
	struct tm timeinfo;
	// 获取当前时间的原始时间数据
	time(&rawtime);
	// 将原始时间数据rawtime转换为本地时间
	localtime_s(&timeinfo, &rawtime);
	// 格式化时间信息
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
}

void handlerRequest(void *lpParam)
{
	char buffer[256];

	while (1)
	{
		int bytes_received = recv(sock_client, buffer, sizeof(buffer), 0);
		if (bytes_received == SOCKET_ERROR)
		{
			error("Receive");
		}
		else
		{
			buffer[bytes_received] = '\0';
			print_time();
			printf("[%s] %s\n", timestamp, buffer);
		}
	}
}

int main()
{
	WSADATA wsaData; // 存储Winsock库的初始化信息
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		error("WSAStartup");
	}

	// 创建socket
	sock_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock_client == INVALID_SOCKET)
	{
		error("Socket creation");
	}

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	// 设置服务器地址，将IP地址转为二进制整数
	server_addr.sin_addr.s_addr = inet_addr("127.127.127.127");
	// 设置服务器端口，小端转大端
	server_addr.sin_port = htons(8000);

	// 输入昵称
	printf("Please enter your name: ");
	char name[256];
	fgets(name, sizeof(name), stdin);
	name[strlen(name) - 1] = '\0';

	// 连接服务器
	if (connect(sock_client, (SOCKADDR *)&server_addr, sizeof(server_addr)))
	{
		error("Connect");
	}

	// 创建线程
	HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)handlerRequest, NULL, 0, NULL);

	// 发送昵称
	int bytes_sent = send(sock_client, name, strlen(name), 0);
	if (bytes_sent == SOCKET_ERROR)
	{
		error("Send");
	}

	while (1)
	{
		char message[256];

		if (fgets(message, sizeof(message), stdin) != NULL)
		{
			size_t len = strlen(message);
			if (len > 0 && message[len - 1] == '\n')
			{
				message[len - 1] = '\0';
			}
		}

		// 构建消息结构体
		size_t message_len = strlen(message);
		struct Message *msg = (struct Message *)malloc(sizeof(struct Message) + message_len - 1);
		if (!msg)
		{
			error("Memory allocation");
		}

		msg->type = 'T'; // 默认为文本类型
		msg->obj[0] = '0';
		msg->obj[1] = '0';
		msg->obj[2] = '0';
		msg->obj[3] = '0';
		msg->obj[4] = '0';
		if (message[0] == '*')
		{
			msg->type = 'S';				   // 类型为系统类型
			strcpy(msg->content, message + 1); // 文本内容，跳过第一个字符 "*"
		}
		else if (message[0] == '@')
		{
			msg->type = 'Q'; // 类型为私聊类型
			char *at_position = strchr(message, '@');
			char *colon_position = strchr(message, ':');
			if (at_position && colon_position && at_position < colon_position)
			{
				// 计算要提取的字符的长度
				int length = colon_position - at_position - 1;

				if (length == 5)
				{
					strncpy(msg->obj, at_position + 1, length);
					strcpy(msg->content, message + 7);
				}
				else
				{
					printf("Input error.\n");
					free(msg);
					continue;
				}
			}
			else
			{
				printf("Input error.\n");
				free(msg);
				continue;
			}
		}
		else
		{
			if (strlen(message) < 1)
			{
				printf("Please input the message.\n");
				free(msg);
				continue;
			}
			strcpy(msg->content, message);
		}

		if (msg->type == 'S' && strcmp(msg->content, "exit") == 0)
		{
			TerminateThread(hThread, 0);
			CloseHandle(hThread);
			free(msg); // 释放动态分配的内存
			break;
		}

		// 发送消息结构体
		int bytes_sent = send(sock_client, (const char *)msg, sizeof(struct Message) + message_len - 1, 0);

		if (bytes_sent == SOCKET_ERROR)
		{
			error("Send");
		}
		else
		{
			print_time();
			printf("[%s] Sent %d bytes to the server: %s\n", timestamp, bytes_sent, msg);
		}

		free(msg); // 释放动态分配的内存
	}

	printf("Bye!\n");
	closesocket(sock_client);
	WSACleanup();
	return 0;
}
