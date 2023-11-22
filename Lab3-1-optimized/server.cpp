#include <winsock2.h>
#include <fstream>
#include <regex>
#include "msg.h"
#include "router.h"

#define CLIENT_PORT htons(8001)
#define CLIENT_IP_ADDRESS "127.0.0.1"
#define SERVER_PORT htons(8000)
#define SERVER_IP_ADDRESS "127.0.0.1"

WSADATA wsaData;
SOCKET serverSock;
SOCKADDR_IN serverAddr;
SOCKADDR_IN clientAddr;
int sockaddrSize = sizeof(sockaddr_in);
char recvBuf[MAX_BUFFER_SIZE];
char sendBuf[MAX_BUFFER_SIZE];
int initialSeqNum;
int initialAckNum;
int sendSize;
string fileName;
int fileSize;

void error(const char *msg)
{
	printf("%s", msg);
	fprintf(stderr, " failed with error %d\n", WSAGetLastError());
	closesocket(serverSock);
	WSACleanup();
	exit(1);
}

void sendflags(unsigned short flag, int seqnum, int acknum)
{
	Message *msg = new Message();
	msg->header.setHeader(flag, seqnum, acknum);
	msg->setChecksum();
	msg->printMsg(true);
	sendSize = sizeof(Header);
	memcpy(sendBuf, msg, sendSize);
	sendWithRegularLoss(serverSock, sendBuf, sendSize, 0, (SOCKADDR *)&clientAddr, sizeof(clientAddr));
	delete msg;
}

void reTransmit(unsigned short flags, int seqnum, int acknum)
{
	Message *msg = new Message();
	msg->header.setHeader(flags, seqnum, acknum);
	msg->setChecksum();
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
	cout << "[Retransmission] ";
	msg->printMsg(true);
	SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN);
	sendSize = sizeof(Header) + msg->header.getLength();
	memcpy(sendBuf, msg, sendSize);
	delete msg;
	sendWithRegularLoss(serverSock, sendBuf, sendSize, 0, (SOCKADDR *)&clientAddr, sizeof(clientAddr));

}

void shakeHands()
{
	Message *recvMsg = new Message();
	while (1)
	{
		// 接收第一次握手
		int bytesRecv = recvfrom(serverSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&clientAddr, &sockaddrSize);
		if (bytesRecv != SOCKET_ERROR)
		{
			recvMsg->reset();
			memcpy(recvMsg, recvBuf, bytesRecv);
			recvMsg->printMsg();
			// 判断标志位和校验和
			if (recvMsg->header.getFlags() == SYN && recvMsg->isValid())
			{
				// 设置 acknumber，消耗一个序列号，ack是发来的seq加1
				initialAckNum = recvMsg->header.getSeqNum() + 1;
				cout << "Successful first handshake." << endl;
				break;
			}
		}
	}

	// 第二次握手，发送ACK报文
	sendflags(ACK, initialSeqNum, initialAckNum);
	delete recvMsg;
}

void prepareSocket()
{
	// initialize Winsock
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		error("WSAStartup");
	}

	// create udp socket
	serverSock = socket(AF_INET, SOCK_DGRAM, 0);
	if (serverSock == INVALID_SOCKET)
	{
		error("Socket creation");
	}

	// client address
	clientAddr.sin_family = AF_INET;
	clientAddr.sin_port = CLIENT_PORT;
	clientAddr.sin_addr.s_addr = inet_addr(CLIENT_IP_ADDRESS);

	// server address
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = SERVER_PORT;
	serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP_ADDRESS);

	// bind the socket
	if (bind(serverSock, (SOCKADDR *)&serverAddr, sizeof(SOCKADDR)) == SOCKET_ERROR)
	{
		error(" Bind");
	}
}

void recvFile()
{
	string filePath = "recv/recv_" + fileName;
	ofstream fileStream(filePath, ios::binary);
	if (!fileStream.is_open())
	{
		cerr << "Error opening file for writing: " << filePath << endl;
		return;
	}
	int remainingSize = fileSize;
	Message *msg = new Message();
	while (remainingSize > 0)
	{
		Message *recvMsg = new Message();
		while (1)
		{
			// 接收数据
			int bytesRecv = recvfrom(serverSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&clientAddr, &sockaddrSize);
			if (bytesRecv != SOCKET_ERROR)
			{
				recvMsg->reset();
				memcpy(recvMsg, recvBuf, bytesRecv);
				// 收到的是正确的文件数据包
				if (recvMsg->isValid() && recvMsg->header.getSeqNum() == initialAckNum && recvMsg->header.getLength())
				{
					recvMsg->printMsg();
					fileStream.write(recvMsg->data, recvMsg->header.getLength());
					remainingSize -= recvMsg->header.getLength();
					initialAckNum += recvMsg->header.getLength();
					sendflags(ACK, initialSeqNum, initialAckNum); // 发送ACK
					break;
				}

				// 收到之前的，回个ACK
				if (recvMsg->isValid() && recvMsg->header.getSeqNum() < initialAckNum)
				{
					reTransmit(ACK, initialSeqNum, recvMsg->header.getSeqNum() + recvMsg->header.getLength());
				}
			}
		}
		delete recvMsg;
	}
	delete msg;
	fileStream.close(); // 关闭文件流
}

bool recvInfo()
{
	Message *recvMsg = new Message();
	while (1)
	{
		int bytesRecv = recvfrom(serverSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&clientAddr, &sockaddrSize);
		if (bytesRecv != SOCKET_ERROR)
		{
			recvMsg->reset();
			memcpy(recvMsg, recvBuf, bytesRecv);

			// 处理接收到的消息
			if (recvMsg->isValid() && recvMsg->header.getSeqNum() == initialAckNum)
			{
				// 收到退出信号，第一次挥手
				if (recvMsg->header.getFlags() == FIN + ACK)
				{
					recvMsg->printMsg(false);
					initialAckNum++; // 第一次挥手消耗序列号
					cout << "Successful first handwaving." << endl;
					return false;
				}
				if (recvMsg->header.getLength())
				{
					// 保存得到文件名和文件的大小
					initialAckNum += recvMsg->header.getLength();
					recvMsg->printMsg(false, true);
					string info(recvMsg->data, recvMsg->header.getLength());
					regex regexPattern("File (.*), Size: (\\d+) bytes");
					smatch match;
					if (regex_match(info, match, regexPattern))
					{
						fileName = match[1];
						fileSize = stoi(match[2]);
					}
					break;
				}
			}
			// 收到了之前发过来的，这可能是因为延时，也可能是因为之前的ACK丢失，回个ACK
			if (recvMsg->isValid() && recvMsg->header.getSeqNum() < initialAckNum)
			{
				reTransmit(ACK, initialSeqNum, recvMsg->header.getSeqNum() + 1);
			}
		}
	}
	sendflags(ACK, initialSeqNum, initialAckNum);
	delete recvMsg;
	return true;
}

// 已经收到第一次挥手后
void closeConnect()
{
	// 发送第二次挥手
	sendflags(ACK, initialSeqNum, initialAckNum);
}

int main()
{
	prepareSocket();

	initialSeqNum = getRandom();

	shakeHands();

	while (1)
	{
		if (!recvInfo())
		{
			break;
		}
		else
		{
			recvFile();
		}
	}

	closeConnect();
	cout << "Bye";
	closesocket(serverSock);
	WSACleanup();
	return 0;
}
