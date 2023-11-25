#include "msg.h"
#include "router.h"
#include "socket.h"
#include "log.h"

int MAXWIN;
char sendBuf[MAX_BUFFER_SIZE];
char recvBuf[MAX_BUFFER_SIZE];
int initialSeqNum, initialAckNum;
int sendSize;
int rounds;
int leftWin, rightWin;
string fileName;
int fileSize;
clock_t start;
int beforeSendNum;
bool ackReceived = false;
mutex ackMutex;
mutex mtx;
char *fileContent;
bool *isFinished;
Logger logger("log/clientLog.txt");
HANDLE *timers;

// 信号处理函数，处理键盘CTRL+C时日志和计时线程的退出
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
		// Output data in a separate log message if requested
		logger.log("[Data]: %s", d);
	}
}

// 发送不包含任何数据的标志位报文
void sendFlag(Message *msg, unsigned short flags, int seqnum, int acknum)
{
	msg->reset();
	msg->header.setHeader(flags, seqnum, acknum);
	msg->setChecksum();
	logMsg(msg, true);
	sendSize = sizeof(Header);
	memcpy(sendBuf, msg, sendSize);
	if (sendWithRegularLoss(clientSock, sendBuf, sendSize, 0, (SOCKADDR *)&serverAddr, sizeof(serverAddr)))
	{
		logger.log("Simulating package loss. This package not sent.");
	}
}

// 超时重传
void reTransmit(Message *msg)
{
	lock_guard<mutex> lock(ackMutex);

	if (!ackReceived && static_cast<double>(clock() - start) / CLOCKS_PER_SEC >= timeOut)
	{
		logger.log("[Retransmission] ");
		logMsg(msg, true);

		if (sendWithRegularLoss(clientSock, sendBuf, sendSize, 0, (SOCKADDR *)&serverAddr, sizeof(serverAddr)))
		{
			logger.log("Simulating package loss. This package not sent.");
		}

		start = clock();
	}
}

// 线程函数 reTransmitThread，处理连接建立阶段和断开阶段以及文件信息包的超时
// 与后面传输文件使用的GBN协议超时逻辑不同
DWORD WINAPI reTransmitThread(LPVOID lpParam)
{
	Message *msg = static_cast<Message *>(lpParam);

	while (true)
	{
		{
			lock_guard<mutex> lock(ackMutex); // Lock ackReceived mutex
			if (ackReceived)
			{
				break;
			}
		}

		reTransmit(msg);
	}

	return 0;
}

// 握手建立连接
void shakeHands()
{
	Message *msg = new Message();
	Message *recvMsg = new Message();
	sendFlag(msg, SYN, initialSeqNum, 0);
	start = clock();
	initialSeqNum++;

	ackReceived = false;
	// 启动线程处理超时
	HANDLE timeoutThread = CreateThread(nullptr, 0, reTransmitThread, msg, 0, nullptr);

	while (true)
	{
		int bytesRecv = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&serverAddr, &sockaddrSize);
		if (bytesRecv != SOCKET_ERROR)
		{
			recvMsg->reset();
			memcpy(recvMsg, recvBuf, bytesRecv);
			if (recvMsg->header.getFlags() == ACK && recvMsg->isValid() && recvMsg->header.getAckNum() == initialSeqNum)
			{
				initialAckNum = recvMsg->header.getSeqNum();
				logMsg(recvMsg);
				logger.log("Successful second handshake.");
				ackReceived = true;
				break;
			}
		}
	}

	WaitForSingleObject(timeoutThread, INFINITE); // 等待超时线程结束
	logger.log("Successfully shake hands with server.");
	delete msg;
	delete recvMsg;
}

char *getFile()
{
	string filePath;
	cin >> filePath; // 输入形如：send/1.jpg
	if (filePath == "exit")
	{
		return nullptr;
	}
	size_t lastSlash = filePath.find_last_of("/");
	fileName = (lastSlash != string::npos) ? filePath.substr(lastSlash + 1) : filePath;

	ifstream fileStream(filePath, ios::binary | ios::ate);
	if (!fileStream.is_open())
	{
		cerr << "Error opening file: " << filePath << endl;
		return nullptr;
	}

	fileSize = static_cast<int>(fileStream.tellg());
	fileStream.seekg(0, ios::beg);
	char *fileBuf = new char[fileSize];
	fileStream.read(fileBuf, fileSize);
	fileStream.close();
	return fileBuf;
}

bool isValidACK(Message *recvMsg)
{
	if (recvMsg->header.getFlags() != ACK)
	{
		logger.log("Expected ACK but not ACK!");
		return false;
	}
	if (!recvMsg->isValid())
	{
		logger.log("Unvalid Message!");
		return false;
	}
	return true;
}

// 发指定序号的包
void sendFilePackage(int number)
{
	Message *fileMessage = new Message();
	int packageDataSize;
	if (number < rounds - 1)
	{
		packageDataSize = MSS;
	}
	else
	{
		packageDataSize = fileSize % MSS;
	}
	char packageBuf[MAX_BUFFER_SIZE];
	fileMessage->header.setHeader(0, beforeSendNum + number, initialAckNum);
	fileMessage->setData(fileContent + number * MSS, packageDataSize);
	fileMessage->setChecksum();
	logMsg(fileMessage, true);
	memcpy(packageBuf, fileMessage, packageDataSize + sizeof(Header));
	if (sendWithRegularLoss(clientSock, packageBuf, packageDataSize + sizeof(Header), 0, (SOCKADDR *)&serverAddr, sizeof(serverAddr)))
	{
		logger.log("Simulating package loss. This package not sent.");
	}
	delete fileMessage;
}

// 文件传输时，接收ACK的线程函数
DWORD WINAPI recvThreadFunction(LPVOID lpParam)
{
	Message *recvMsg = new Message();
	char recvFileBuf[MAX_BUFFER_SIZE];
	while (1)
	{
		int bytesRecv = recvfrom(clientSock, recvFileBuf, sizeof(recvFileBuf), 0, (SOCKADDR *)&serverAddr, &sockaddrSize);

		if (bytesRecv != SOCKET_ERROR)
		{
			recvMsg->reset();
			memcpy(recvMsg, recvFileBuf, bytesRecv);
			// 判断标志位 ACK 和校验和
			logMsg(recvMsg);

			if (isValidACK(recvMsg))
			{
				mtx.lock();
				if (recvMsg->header.getAckNum() <= rightWin + beforeSendNum)
				{
					isFinished[recvMsg->header.getAckNum() - beforeSendNum - 1] = true;
					if (leftWin == recvMsg->header.getAckNum() - beforeSendNum - 1)
					{
						for (leftWin; leftWin < rounds; leftWin++)
						{
							if (!isFinished[leftWin])
							{
								break;
							}
						}
					}
					logMsg(recvMsg);
					logger.log("[Window][AFTER RECV] LEFT: %d, RIGHT: %d", leftWin + beforeSendNum, rightWin + beforeSendNum);
				}
				mtx.unlock();
			}
		}
	}

	delete recvMsg;
	return 0;
}

// 当超时时，需要重传窗口内的所有包，这里直接将右边界拉回左边界
DWORD WINAPI reTransmitFileThreadFunction(LPVOID lpParam)
{
	intptr_t pkg = reinterpret_cast<intptr_t>(lpParam);
	clock_t start = clock();
	while (!isFinished[pkg])
	{
		if (static_cast<double>(clock() - start) / CLOCKS_PER_SEC >= timeOut)
		{
			logger.log("retransimit %d", pkg + beforeSendNum);
			sendFilePackage(pkg);
			start = clock();
		}
	}
	return 0;
}

// 发送文件，使用 GBN 协议
void sendFile()
{
	rounds = fileSize / MSS + 1;
	timers = new HANDLE[rounds];
	isFinished = new bool[rounds];
	for (int i = 0; i < rounds; i++)
	{
		isFinished[i] = false;
	}
	beforeSendNum = initialSeqNum;
	leftWin = rightWin = 0;
	HANDLE recvThread = CreateThread(nullptr, 0, recvThreadFunction, 0, 0, nullptr); // 接收ACK线程
	while (1)
	{
		// 窗口未满，未发送完，可以发送
		if (rightWin - leftWin < MAXWIN && rightWin < rounds)
		{
			sendFilePackage(rightWin);
			timers[rightWin] = CreateThread(nullptr, 0, reTransmitFileThreadFunction, reinterpret_cast<LPVOID>(rightWin), 0, nullptr);
			rightWin++; // 发送后，右窗口右移
			logger.log("[Window][AFTER SENT] LEFT: %d, RIGHT: %d", leftWin + beforeSendNum, rightWin + beforeSendNum);
		}
		if (leftWin == rounds) // 当所有包都发送结束且确认结束后，结束循环
		{
			break;
		}
	}
	TerminateThread(recvThread, 0);
	CloseHandle(recvThread);
}

// 先发送文件信息并得到确认
void sendFileInfo()
{
	Message *fileInfo = new Message();
	Message *recvMsg = new Message();
	fileInfo->header.setHeader(0, initialSeqNum, initialAckNum);
	int lenInfo = fileName.length() + to_string(fileSize).length() + 19;
	char tmpBuf[lenInfo + 1];
	sprintf(tmpBuf, "File %s, Size: %d bytes", fileName.c_str(), fileSize);
	fileInfo->setData(tmpBuf, lenInfo);
	fileInfo->setChecksum();
	logMsg(fileInfo, true, true);
	sendSize = sizeof(Header) + fileInfo->header.getLength();
	memcpy(sendBuf, fileInfo, sendSize);
	if (sendWithRegularLoss(clientSock, sendBuf, sendSize, 0, (SOCKADDR *)&serverAddr, sizeof(serverAddr)))
	{
		logger.log("Simulating package loss. This package not sent.");
	}

	start = clock();
	ackReceived = false;
	// 启动线程处理超时
	HANDLE timeoutThread = CreateThread(nullptr, 0, reTransmitThread, fileInfo, 0, nullptr);

	while (1)
	{
		int bytesRecv = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&serverAddr, &sockaddrSize);
		if (bytesRecv != SOCKET_ERROR)
		{
			recvMsg->reset();
			memcpy(recvMsg, recvBuf, bytesRecv);
			// 判断标志位和校验和，判断收到的ACK是否是想要的ACK
			if (isValidACK(recvMsg))
			{
				if (recvMsg->header.getAckNum() == initialSeqNum + 1)
				{
					logMsg(recvMsg);
					logger.log("Successful send file information.");
					ackReceived = true;
					break;
				}
				else
				{
					logger.log("Unvalid ACK number.");
				}
			}
		}
	}

	WaitForSingleObject(timeoutThread, INFINITE); // 等待超时线程结束

	initialSeqNum++;
	delete fileInfo;
	delete recvMsg;

	clock_t fileStart = clock();
	sendFile(); // 传输文件
	clock_t fileEnd = clock();
	initialSeqNum += rounds;
	logger.log("Successfully send file %s.", fileName.c_str());
	logger.log("Transmit rounds: %d", rounds);
	logger.log("Total time: %.2f s", static_cast<double>(fileEnd - fileStart) / CLOCKS_PER_SEC);
	logger.log("Throughput: %.2f byte/s", static_cast<double>(fileSize + rounds * sizeof(Header)) / (fileEnd - fileStart) * CLOCKS_PER_SEC);
}

// 两次挥手断开连接
void closeConnect()
{
	Message *recvMsg = new Message();
	Message *msg = new Message();
	// 第一次挥手，发送 FIN+ACK，第一次挥手消耗序列号
	sendFlag(msg, FIN + ACK, initialSeqNum, initialAckNum);
	start = clock();
	initialSeqNum++; // 确认完已成功发送后，序列号加1

	ackReceived = false;
	HANDLE timeoutThread = CreateThread(nullptr, 0, reTransmitThread, msg, 0, nullptr);

	// 收第二次挥手，没有收到就重发
	while (1)
	{
		int bytesRecv = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&serverAddr, &sockaddrSize);
		if (bytesRecv != SOCKET_ERROR)
		{
			recvMsg->reset();
			memcpy(recvMsg, recvBuf, bytesRecv);
			// 判断标志位和校验和
			if (recvMsg->header.getFlags() == ACK && recvMsg->isValid())
			{
				logMsg(recvMsg);
				logger.log("Successful second handwaving.");
				ackReceived = true;
				break;
			}
		}
	}
	WaitForSingleObject(timeoutThread, INFINITE);
	delete recvMsg;
	delete msg;
}

int main()
{
	signal(SIGINT, signalHandler);
	double a;
	int b;
	logger.log("Please input the window size.");
	cin >> MAXWIN;
	logger.log("Please input miss rate.");
	cin >> a;
	logger.log("Please input delay time.");
	cin >> b;
	setValue(a, b);

	prepareSocket();
	initialSeqNum = 0;

	shakeHands(); // 握手建立连接
	logger.log("Please input the filePath to send.");
	fileContent = getFile(); // 获取要传输的文件
	if (fileContent)		 // 传输文件信息
	{
		sendFileInfo();
	}
	logger.log("Please enter exit to exit.");

	if (getFile() == nullptr)
	{
		closeConnect(); // 断开连接
	}
	logger.log("Bye");
	closesocket(clientSock);
	WSACleanup();
	return 0;
}
