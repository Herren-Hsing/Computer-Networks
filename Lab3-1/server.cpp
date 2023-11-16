#include <winsock2.h>
#include <fstream>
#include <regex>
#include "msg.h"

#define CLIENT_PORT htons(8002)
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
	if (sendto(serverSock, sendBuf, sendSize, 0, (SOCKADDR *)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR)
	{
		error("Send");
	}
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
	if (sendto(serverSock, sendBuf, sendSize, 0, (SOCKADDR *)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR)
	{
		error("Send");
	}
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
				cout << "Successful first handshake" << endl;
				break;
			}
		}
	}

	// 第二次握手，发送SYN+ACK报文
	sendflags(SYN + ACK, initialSeqNum, initialAckNum);
	initialSeqNum++;

	// 如果收第三次握手时，收到了第一次握手，那么就说明第二次握手丢失了，或者这个第一次握手是delay的，需要重新发送第二次握手
	// 因为如果是第二次握手丢失的话，不重发，客户端就永远也收不到第二次握手了
	while (1)
	{
		// 收第三次握手
		int bytesRecv = recvfrom(serverSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&clientAddr, &sockaddrSize);

		if (bytesRecv != SOCKET_ERROR)
		{
			recvMsg->reset();
			memcpy(recvMsg, recvBuf, bytesRecv);
			// 要判断序列号
			// 第三次握手报文不消耗序列号，ack不变
			if (recvMsg->header.getFlags() == ACK && recvMsg->isValid() && recvMsg->header.getSeqNum() == initialAckNum)
			{
				recvMsg->printMsg();
				cout << "Successful third handshake" << endl;
				break;
			}
			// 重发第二次握手
			if (recvMsg->header.getFlags() == SYN && recvMsg->isValid() && recvMsg->header.getSeqNum() == initialAckNum - 1)
			{
				reTransmit(SYN + ACK, initialSeqNum - 1, recvMsg->header.getSeqNum() + 1);
			}
		}
	}

	// 这里为了防止出现第三次握手的ACK丢失的情况，增加服务器对客户端第三次握手的ACK响应
	// 如果响应丢失怎么办？客户端会重发，而在后面的处理中，如果收到了来自客户端的 seq序号小的，就是说明是ack丢失情况，就回个ack就行了
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
					cout << "Successful first handwaving" << endl;
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
	u_long mode = 1;
	ioctlsocket(serverSock, FIONBIO, &mode);
	Message *recvMsg = new Message();
	// 发送第二次挥手  第二次挥手丢失怎么办？这时客户端重发第一次挥手
	sendflags(ACK + FIN, initialSeqNum, initialAckNum);
	clock_t start = clock();
	// 收第三次挥手
	while (1)
	{
		int bytesRecv = recvfrom(serverSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&clientAddr, &sockaddrSize);

		if (bytesRecv != SOCKET_ERROR)
		{
			recvMsg->reset();
			memcpy(recvMsg, recvBuf, bytesRecv);
			// 对于可能时延过长的报文，不用处理；但是如果这个阶段还收到了FIN+ACK，说明第二次挥手丢失。重发第二次挥手；
			if (recvMsg->isValid() && recvMsg->header.getSeqNum() == initialAckNum - 1 && recvMsg->header.getFlags() == FIN + ACK)
			{
				reTransmit(FIN + ACK, initialSeqNum, initialAckNum);
				start = clock();
			}
			// 说明第二次挥手发送成功
			if (recvMsg->isValid() && recvMsg->header.getFlags() == ACK && recvMsg->header.getSeqNum() == initialAckNum && recvMsg->header.getAckNum() == initialSeqNum + 1)
			{
				recvMsg->printMsg();
				cout << "Successful third handwaving" << endl;
				initialSeqNum++;
				break;
			}
		}
		// 如果超时收不到ACK，重发第二次挥手
		if (static_cast<double>(clock() - start) / CLOCKS_PER_SEC >= time_out)
		{
			reTransmit(FIN + ACK, initialSeqNum, initialAckNum);
			start = clock();
		}
	}
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
