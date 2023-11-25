#include <regex>
#include <map>
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
const double waitTime = 1;
int recvBase;
int beforeRecv;
int rounds;
int MAXWIN;

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

void sendflags(unsigned short flag, int seqnum, int acknum)
{
	Message *msg = new Message();
	msg->header.setHeader(flag, seqnum, acknum);
	msg->setChecksum();
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
	recvBase = initialAckNum;
	beforeRecv = initialAckNum;
	rounds = fileSize / MSS + 1;
	Message *msg = new Message();
	Message *recvMsg = new Message();
	bool isRecv[rounds] = {0};
	int remainingSize = fileSize;
	map<int, char[MSS + 1]> fileBuf;
	map<int, int> fileBufSize;
	while (remainingSize > 0)
	{
		// 接收数据
		int bytesRecv = recvfrom(serverSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&clientAddr, &sockaddrSize);
		if (bytesRecv != SOCKET_ERROR)
		{
			recvMsg->reset();
			memcpy(recvMsg, recvBuf, bytesRecv);
			// 收到的是正确的文件数据包
			logMsg(recvMsg);
			if (recvMsg->isValid())
			{
				if (recvMsg->header.getSeqNum() < recvBase && recvMsg->header.getSeqNum() >= recvBase - MAXWIN)
				{
					logger.log("Unordered");
					sendflags(ACK, initialSeqNum, recvMsg->header.getSeqNum() + 1); // 发送ACK
				}
				// 收到的分组落在接收方窗口内：回复ACK，如果没收到过该分组，缓存该分组；
				else if (recvMsg->header.getSeqNum() >= recvBase && recvMsg->header.getSeqNum() < recvBase + MAXWIN)
				{
					sendflags(ACK, initialSeqNum, recvMsg->header.getSeqNum() + 1);
					// 第一次收到，放入缓存区
					if (!isRecv[recvMsg->header.getSeqNum() - beforeRecv])
					{
						isRecv[recvMsg->header.getSeqNum() - beforeRecv] = true;
						memcpy(fileBuf[recvMsg->header.getSeqNum() - beforeRecv], recvMsg->data, recvMsg->header.getLength());
						fileBufSize[recvMsg->header.getSeqNum() - beforeRecv] = recvMsg->header.getLength();
					}
					if (recvMsg->header.getSeqNum() == recvBase)
					{
						// 交付连续接收到的并收缩左边界
						int i = recvBase;
						for (i; i < rounds + beforeRecv; i++)
						{
							if (isRecv[i - beforeRecv])
							{
								logger.log("write %d Size %d", i - beforeRecv, fileBufSize[i - beforeRecv]);
								fileStream.write(fileBuf[i - beforeRecv], fileBufSize[i - beforeRecv]);
								remainingSize -= fileBufSize[i - beforeRecv];
							}
							else
							{
								break;
							}
						}
						recvBase = i;
					}
					logger.log("remainingSize %d", remainingSize);
					logger.log("rcvBase %d", recvBase);
				}
			}
		}
	}
	initialAckNum = recvBase;
	delete msg;
	delete recvMsg;
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
				sendflags(ACK, initialSeqNum, recvMsg->header.getSeqNum() + 1);
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
	logger.log("Please input the window size.");
	cin >> MAXWIN;
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