#include <Winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <string.h>
#include <time.h>

SOCKET sock_server;             // 服务器套接字
struct sockaddr_in server_addr; // 服务器地址结构体
CRITICAL_SECTION cs;

#define MAX_CLIENTS 100
static int num_clients = 0; // 当前连接的客户端数量

struct client_information // 客户端信息结构体
{
    SOCKET socket;           // 客户端套接字
    struct sockaddr_in addr; // 客户端地址信息
    char *name;              // 客户端昵称
    int id;                  // 客户端id
};
struct client_information clients[MAX_CLIENTS];

char recv_msg[256];
char broad_msg[256];
char timestamp[20];
int bytes_recv;
int id = 0;

void error(char *msg)
{
    printf("%s", msg);
    fprintf(stderr, " failed with error %d\n", WSAGetLastError());
    closesocket(sock_server);
    WSACleanup();
    exit(1);
}

void print_time()
{
    time_t rawtime;
    struct tm timeinfo;
    time(&rawtime);
    localtime_s(&timeinfo, &rawtime);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
}

// 广播消息给所有客户端
void broadcast(struct client_information *clients_info)
{
    EnterCriticalSection(&cs); // 进入临界区
    // 遍历所有已连接的客户端
    for (int i = 0; i < num_clients; i++)
    {
        if (clients_info->socket != clients[i].socket && clients_info->name != NULL)
        {
            // 发送消息
            send(clients[i].socket, broad_msg, strlen(broad_msg), 0);
        }
    }
    LeaveCriticalSection(&cs); // 离开临界区
}

// 处理客户端退出
void handle_exit(struct client_information *clients_info)
{
    EnterCriticalSection(&cs);
    // 打印客户端退出信息
    print_time();
    printf("[%s] Client disconnected: %s:%d\n", timestamp, inet_ntoa(clients_info->addr.sin_addr), ntohs(clients_info->addr.sin_port));
    sprintf(broad_msg, "Client(%d) %s(%s:%d) is offline!", clients_info->id, clients_info->name, inet_ntoa(clients_info->addr.sin_addr), ntohs(clients_info->addr.sin_port));
    broadcast(clients_info);
    // 释放客户端名称内存
    free(clients_info->name);
    closesocket(clients_info->socket);
    // 查找要删除的客户端
    int i;
    for (i = 0; i < num_clients; i++)
    {
        if (clients[i].socket == clients_info->socket)
            break;
    }
    // 将要删除的客户端信息从数组中移除
    if (i < num_clients)
    {
        memmove(&clients[i], &clients[i + 1], (num_clients - i - 1) * sizeof(struct client_information));
        num_clients--;
    }
    LeaveCriticalSection(&cs);
}

DWORD WINAPI handlerRequest(LPVOID lparam)
{
    struct client_information *clients_info = (struct client_information *)lparam; // 客户端信息

    bytes_recv = recv(clients_info->socket, recv_msg, sizeof(recv_msg) - 1, 0);
    recv_msg[bytes_recv] = '\0';

    // 存放客户端的昵称
    clients_info->name = (char *)malloc(strlen(recv_msg) + 1);
    strcpy(clients_info->name, recv_msg);

    char sendBuf[] = "Welcome to the chat room. Enter your message and press Enter to send. Enter '*' and the content you wish to use for executing system commands. If you wanna chat to somebody, please input like '@00001:xxxxx'(length of id must be five)";
    send(clients_info->socket, sendBuf, strlen(sendBuf), 0);

    // 向所有已连接的其他客户端广播上线通知
    sprintf(broad_msg, "Client(%d) %s (%s:%d) is online!", clients_info->id, clients_info->name, inet_ntoa(clients_info->addr.sin_addr), ntohs(clients_info->addr.sin_port));
    broadcast(clients_info);

    while (1)
    {
        // 接受客户端消息
        bytes_recv = recv(clients_info->socket, recv_msg, sizeof(recv_msg) - 1, 0);
        if (bytes_recv == SOCKET_ERROR || bytes_recv == 0)
        {
            handle_exit(clients_info); // 处理客户端退出
            break;                     // 退出循环，结束线程
        }
        recv_msg[bytes_recv] = '\0';

        // 打印从客户端接收到的消息
        print_time();
        printf("[%s] Received from %s(%s:%d): %s\n", timestamp, clients_info->name, inet_ntoa(clients_info->addr.sin_addr), ntohs(clients_info->addr.sin_port), recv_msg);

        // 判断消息类型
        char message_type = recv_msg[0];

        // 提取内容
        char *message_content = &recv_msg[1]; // 从第二个字符开始是消息内容

        if (message_type == 'T')
        {
            // 将接收到的消息发送给所有已连接的客户端
            sprintf(broad_msg, "(%d)%s(%s:%d): %s", clients_info->id, clients_info->name, inet_ntoa(clients_info->addr.sin_addr), ntohs(clients_info->addr.sin_port), message_content + 5);
            broadcast(clients_info);
        }
        else if (message_type == 'Q')
        {
            int obj = (message_content[0] - '0') * 10000 + (message_content[1] - '0') * 1000 + (message_content[2] - '0') * 100 + (message_content[3] - '0') * 10 + (message_content[4] - '0');
            if (obj > 0)
            {
                sprintf(broad_msg, "[private](%d) %s(%s:%d): %s", clients_info->id, clients_info->name, inet_ntoa(clients_info->addr.sin_addr), ntohs(clients_info->addr.sin_port), message_content + 5);
                int i;

                EnterCriticalSection(&cs);
                for (i = 0; i < num_clients; i++)
                {
                    if (clients[i].id == obj)
                        break;
                }
                if (i == num_clients)
                {
                    char sendBuf[] = "Sent failed!Input the right ID!";
                    send(clients_info->socket, sendBuf, strlen(sendBuf), 0);
                    continue;
                }
                send(clients[i].socket, broad_msg, strlen(broad_msg), 0);
                LeaveCriticalSection(&cs);
            }
            else
            {
                char sendBuf[] = "Sent failed!Input the right ID!";
                send(clients_info->socket, sendBuf, strlen(sendBuf), 0);
                continue;
            }
        }
    }
    return 0;
}

int main()
{
    // 初始化Winsock库
    WSADATA wsaData; // 存储Winsock库的初始化信息
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        error("WSAStartup");
    }

    // 创建服务器welcome套接字
    sock_server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_server == INVALID_SOCKET)
    {
        error("Socket creation");
    }

    // 初始化服务器地址结构体
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    // 设置服务器 IP 地址，将 IP 地址转为二进制整数
    server_addr.sin_addr.s_addr = inet_addr("127.127.127.127");
    // 设置服务器端口，小端转大端
    server_addr.sin_port = htons(8000);

    // 服务器套接字 sock_server 被绑定到 server_addr 所指定的IP地址和端口号上
    if (bind(sock_server, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        error("Socket bind");
    }

    // 监听客户端连接请求
    if (listen(sock_server, MAX_CLIENTS))
    {
        error("Listen");
    }

    InitializeCriticalSection(&cs);

    while (1)
    {
        // 对每个客户端，等待连接请求，创建新的连接socket
        int client_addr_len = sizeof(clients[num_clients].addr);
        struct client_information tmp;
        tmp.socket = accept(sock_server, (struct sockaddr *)&tmp.addr, &client_addr_len);
        clients[num_clients].socket = tmp.socket;
        clients[num_clients].addr = tmp.addr;

        if (clients[num_clients].socket == INVALID_SOCKET)
        {
            printf("Socket accept error");
            break;
        }
        else if (num_clients >= MAX_CLIENTS)
        {
            printf("Maximum number of clients reached. Not accepting new connections.\n");
            // 发送错误消息给客户端
            const char *error_message = "Server is at maximum capacity. Try again later.";
            send(clients[num_clients].socket, error_message, strlen(error_message), 0);

            // 关闭与该客户端的连接
            closesocket(clients[num_clients].socket);
        }
        else
        {
            id++;
            clients[num_clients].id = id;
            print_time();
            // 打印连接信息
            printf("[%s] Client connected from: %s:%d\n", timestamp, inet_ntoa(clients[num_clients].addr.sin_addr), ntohs(clients[num_clients].addr.sin_port));
            // 创建线程
            HANDLE hThreads = CreateThread(NULL, 0, handlerRequest, &clients[num_clients], 0, NULL);
            if (hThreads == NULL)
            {
                printf("Thread creation error");
                closesocket(clients[num_clients].socket);
            }
            CloseHandle(hThreads);
            num_clients++;
        }
    }

    DeleteCriticalSection(&cs);
    closesocket(sock_server);
    WSACleanup();
    return 0;
}
