#include <regex>
#include "msg.h"
#include "router.h"
#include "log.h"
#include "socket.h"

Logger logger("log/serverLog.txt"); // 服务器日志文件

char recvBuf[MAX_BUFFER_SIZE];
char sendBuf[MAX_BUFFER_SIZE];
int initialSeqNum;
int initialAckNum;
int sendSize;
string fileName;
int fileSize;
const double waitTime = 5;

// 信号处理函数，处理键盘CTRL+C时日志线程的退出
void signalHandler(int signum)
{
	if (signum == SIGINT)
	{
		logger.stopLog = true;		   // 设置停止标志
		logger.condition.notify_one(); // 通知日志线程，以确保它从条件变量等待中醒来
		logger.logFile.close();		   // 关闭日志文件
		exit(signum);				   // 退出程序
	}
}

void logMsg(Message *msg, bool isSender = false, bool printData = false)
{
	if (isSender)
	{
		logger.log("[SEND] Package [SYN:%d] [ACK:%d] [FIN:%d] [seq num:%d] [ack num:%d] [CheckSum:%d] [Data Length:%d]",
				   msg->header.isSYN(), msg->header.isACK(), msg->header.isFIN(), msg->header.getSeqNum(), msg->header.getAckNum(),
				   msg->header.getChecksum(), msg->header.getLength());
	}
	else
	{
		logger.log("[RECV] Package [SYN:%d] [ACK:%d] [FIN:%d] [seq num:%d] [ack num:%d] [CheckSum:%d] [Data Length:%d]",
				   msg->header.isSYN(), msg->header.isACK(), msg->header.isFIN(), msg->header.getSeqNum(), msg->header.getAckNum(),
				   msg->header.getChecksum(), msg->header.getLength());
	}

	char d[msg->header.getLength() + 1];
	strncpy(d, msg->data, msg->header.getLength());
	d[msg->header.getLength()] = '\0';
	if (printData)
	{
		logger.log("[Data]: %s", d);
	}
}

void sendflags(unsigned short flag, int seqnum, int acknum, bool isDisordered = false)
{
	Message *msg = new Message();
	msg->header.setHeader(flag, seqnum, acknum);
	msg->setChecksum();
	if (isDisordered)
	{
		logger.log("[Disordered ACK]");
	}
	logMsg(msg, true);
	sendSize = sizeof(Header);
	memcpy(sendBuf, msg, sendSize);
	if (sendWithRegularLoss(serverSock, sendBuf, sendSize, 0, (SOCKADDR *)&clientAddr, sizeof(clientAddr)))
	{
		logger.log("Simulating package loss. This package not sent.");
	}

	delete msg;
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
			logMsg(recvMsg);
			// 判断标志位和校验和
			if (recvMsg->header.getFlags() == SYN && recvMsg->isValid())
			{
				// 设置 acknumber，消耗一个序列号，ack是发来的seq加1
				initialAckNum = recvMsg->header.getSeqNum() + 1;
				logger.log("Successful first handshake.");
				break;
			}
		}
	}

	// 第二次握手，发送ACK报文
	sendflags(ACK, initialSeqNum, initialAckNum);
	delete recvMsg;
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
				if (recvMsg->isValid())
				{
					logMsg(recvMsg);
					// 收到失序数据包，回复最近的有序的ACK序号
					if (recvMsg->header.getSeqNum() != initialAckNum)
					{
						sendflags(ACK, initialSeqNum, initialAckNum, true);
					}
					else if (recvMsg->header.getSeqNum() == initialAckNum && recvMsg->header.getLength())
					{
						fileStream.write(recvMsg->data, recvMsg->header.getLength());
						remainingSize -= recvMsg->header.getLength();
						initialAckNum++;
						sendflags(ACK, initialSeqNum, initialAckNum); // 发送ACK
						break;
					}
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
					logMsg(recvMsg, false);
					initialAckNum++; // 第一次挥手消耗序列号
					logger.log("Successful first handwaving.");
					return false;
				}
				if (recvMsg->header.getLength())
				{
					// 保存得到文件名和文件的大小
					initialAckNum++;
					logMsg(recvMsg, false, true);
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
			if (recvMsg->isValid() && recvMsg->header.getSeqNum() < initialAckNum)
			{
				sendflags(ACK, initialSeqNum, recvMsg->header.getSeqNum() + 1, true);
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
	clock_t start = clock();
	Message *recvMsg = new Message();
	u_long mode = 1;
	ioctlsocket(serverSock, FIONBIO, &mode);
	// 退出前等待一会
	while (1)
	{
		int bytesRecv = recvfrom(serverSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&clientAddr, &sockaddrSize);
		if (bytesRecv != SOCKET_ERROR)
		{
			recvMsg->reset();
			memcpy(recvMsg, recvBuf, bytesRecv);
			if (recvMsg->isValid())
			{
				logMsg(recvMsg, false);
				sendflags(ACK, initialSeqNum, initialAckNum);
			}
		}
		// 等待一定时间
		if (static_cast<double>(clock() - start) / CLOCKS_PER_SEC >= waitTime)
		{
			break;
		}
	}
	delete recvMsg;
}

int main()
{
	signal(SIGINT, signalHandler);
	double a;
	int b;
	logger.log("Please input miss rate.");
	cin >> a;
	logger.log("Please input delay time.");
	cin >> b;
	setValue(a, b);
	prepareSocket(false);

	initialSeqNum = 0;

	shakeHands();

	if (recvInfo())
	{
		recvFile();
		recvInfo();
		closeConnect();
	}
	else
	{
		closeConnect();
	}
	logger.log("Bye.");
	closesocket(serverSock);
	WSACleanup();
	return 0;
}