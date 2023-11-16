#include <winsock2.h>
#include <stdio.h>
#include <fstream>
#include <vector>
#include <cstring>
#include <iomanip>
#include "msg.h"

#define CLIENT_PORT htons(8001)
#define CLIENT_IP_ADDRESS "127.0.0.1"
#define SERVER_PORT htons(8002)
#define SERVER_IP_ADDRESS "127.0.0.1"

WSADATA wsaData;
SOCKET clientSock;
SOCKADDR_IN serverAddr;
SOCKADDR_IN clientAddr;
char sendBuf[MAX_BUFFER_SIZE];
char recvBuf[MAX_BUFFER_SIZE];
int sockaddrSize = sizeof(sockaddr_in);
int initialSeqNum;
int initialAckNum;
int sendSize;
string fileName;
int fileSize;
clock_t start;
int reTransmitCount = 0;

void error(const char *msg)
{
    printf("%s", msg);
    fprintf(stderr, " failed with error %d\n", WSAGetLastError());
    closesocket(clientSock);
    WSACleanup();
    exit(1);
}

void prepareSocket()
{
    // initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        error("WSAStartup");
    }

    // create udp socket
    clientSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSock == INVALID_SOCKET)
    {
        error("Socket creation");
    }

    // client address
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = CLIENT_PORT;
    clientAddr.sin_addr.s_addr = inet_addr(CLIENT_IP_ADDRESS);
    // bind the socket
    if (bind(clientSock, (SOCKADDR *)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR)
    {
        error("Bind");
    }

    // server address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = SERVER_PORT;
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP_ADDRESS);

    u_long mode = 1;
    ioctlsocket(clientSock, FIONBIO, &mode);
}

// 超时重传
void reTransmit(Message *msg)
{
    if (static_cast<double>(clock() - start) / CLOCKS_PER_SEC >= time_out)
    {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        cout << "[Retransmission][" << reTransmitCount++ << "] ";
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN);
        msg->printMsg(true);
        if (sendto(clientSock, sendBuf, sendSize, 0, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            error("Send");
        }
        start = clock();
    }
    // 异常处理，超时重传多次，可能连接异常，断开连接
    if (reTransmitCount >= MAX_RETANSMIT)
    {
        error("Connect");
    }
}

// 发送不包含任何数据的标志位报文
void sendFlag(Message *msg, unsigned short flags, int seqnum, int acknum)
{
    msg->reset();
    msg->header.setHeader(flags, seqnum, acknum);
    msg->setChecksum();
    msg->printMsg(true);
    sendSize = sizeof(Header);
    memcpy(sendBuf, msg, sendSize);
    if (sendto(clientSock, sendBuf, sendSize, 0, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        error("Send");
    }
}

// 三次握手建立连接
void shakeHands()
{
    Message *msg = new Message();
    Message *recvMsg = new Message();
    // 第一次握手，客户端发送SYN报文，seq为初始值（设置为0），ack为0
    sendFlag(msg, SYN, initialSeqNum, 0);
    start = clock();
    reTransmitCount = 0;

    // 如果第一次握手丢失，一定时间内收不到服务器发来的第二次握手，需要重发第一次握手
    // 而如果第二次握手丢失，也是重发第一次握手，但这个时候，服务器已经进入准备接收第三次握手了
    // 第二次握手，接收服务器发送的SYN+ACK报文
    while (1)
    {
        int bytesRecv = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&serverAddr, &sockaddrSize);
        if (bytesRecv != SOCKET_ERROR)
        {
            recvMsg->reset();
            memcpy(recvMsg, recvBuf, bytesRecv);
            // 判断标志位和校验和
            if (recvMsg->header.getFlags() == SYN + ACK && recvMsg->isValid())
            {
                // 设置下一条发送的报文的ack值，收到的第二次握手是消耗序号的
                initialAckNum = recvMsg->header.getSeqNum() + 1;
                recvMsg->printMsg();
                cout << "Successful second handshake" << endl;
                break;
            }
        }
        // 超时重传
        reTransmit(msg);
    }

    // 第三次握手，发送ACK报文，seq++是因为第一条报文消耗了一个序号，ack值是根据服务器发来报文的seq值加1后的结果设置的
    sendFlag(msg, ACK, ++initialSeqNum, initialAckNum);
    start = clock();
    reTransmitCount = 0;

    // 这里为了防止出现第三次握手的ACK丢失的情况，增加服务器对客户端第三次握手的ACK响应
    // 如果收不到响应，则第三次握手丢失，重发
    while (1)
    {
        int bytesRecv = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&serverAddr, &sockaddrSize);
        if (bytesRecv != SOCKET_ERROR)
        {
            recvMsg->reset();
            memcpy(recvMsg, recvBuf, bytesRecv);
            // 判断标志位和校验和
            if (recvMsg->header.getFlags() == ACK && recvMsg->isValid() && recvMsg->header.getSeqNum() == initialAckNum)
            {
                recvMsg->printMsg();
                cout << "Successfully shake hands with server!" << endl;
                break;
            }
        }
        reTransmit(msg);
    }

    delete msg;
    delete recvMsg;
}

char *getFile()
{
    string filePath;
    cout << "Please input the filePath to send." << endl;
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

bool isValidACK(Message *recvMsg, int lastLength)
{
    if (recvMsg->header.getFlags() != ACK)
    {
        cout << "Expected ACK but not ACK!" << endl;
        return false;
    }
    if (!recvMsg->isValid())
    {
        cout << "Unvalid Message!" << endl;
        return false;
    }
    if (recvMsg->header.getAckNum() != initialSeqNum + lastLength)
    {
        cout << "Unvalid ack number!" << endl;
        return false;
    }
    if (recvMsg->header.getSeqNum() != initialAckNum)
    {
        cout << "Unvalid seq number!" << endl;
        return false;
    }
    return true;
}

// 发送文件
void sendFile(char *fileContent, int &rounds)
{
    rounds = 0;
    int remainingSize = fileSize;
    int segSize;
    Message *fileMessage = new Message();
    Message *recvMsg = new Message();
    while (remainingSize > 0)
    {
        rounds++;
        // 每次发送的字节数
        segSize = (remainingSize > MSS ? MSS : remainingSize);
        fileMessage->reset();
        fileMessage->header.setHeader(0, initialSeqNum, initialAckNum);
        fileMessage->setData(fileContent, segSize);
        fileMessage->setChecksum();
        sendSize = sizeof(Header) + segSize;
        memcpy(sendBuf, fileMessage, sendSize);
        if (sendto(clientSock, sendBuf, sendSize, 0, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            error("Send");
        }
        start = clock();
        reTransmitCount = 0;
        fileMessage->printMsg(true);

        // 获取ACK，一段时间没获取到，超时重传：防止自己的文件丢失，对方的ACK丢失
        // 对于旧的ACK，leave it alone
        while (1)
        {
            int bytesRecv = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&serverAddr, &sockaddrSize);
            if (bytesRecv != SOCKET_ERROR)
            {
                recvMsg->reset();
                memcpy(recvMsg, recvBuf, bytesRecv);
                // 判断标志位和校验和，需要判断是正确的ACK，正确的ACK的acknum就是现在的initialseqnumber
                if (isValidACK(recvMsg, segSize))
                {
                    recvMsg->printMsg();
                    cout << "Successful send file segment. Already send ";
                    cout << fixed << setprecision(2) << static_cast<double>(fileSize - remainingSize + segSize) / fileSize * 100 << "%." << endl;
                    break;
                }
            }
            reTransmit(fileMessage);
        }
        remainingSize -= segSize;
        fileContent += segSize;
        initialSeqNum += segSize;
    }
}

// 发送文件信息
void sendFileInfo(char *fileContent)
{
    Message *fileInfo = new Message();
    Message *recvMsg = new Message();
    fileInfo->header.setHeader(0, initialSeqNum, initialAckNum);
    int lenInfo = fileName.length() + to_string(fileSize).length() + 19;
    char tmpBuf[lenInfo + 1];
    sprintf(tmpBuf, "File %s, Size: %d bytes", fileName.c_str(), fileSize);
    fileInfo->setData(tmpBuf, lenInfo);
    fileInfo->setChecksum();
    fileInfo->printMsg(true, true);
    sendSize = sizeof(Header) + fileInfo->header.getLength();
    memcpy(sendBuf, fileInfo, sendSize);
    if (sendto(clientSock, sendBuf, sendSize, 0, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        error("Send");
    }
    start = clock();
    reTransmitCount = 0;

    while (1)
    {
        int bytesRecv = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&serverAddr, &sockaddrSize);
        if (bytesRecv != SOCKET_ERROR)
        {
            recvMsg->reset();
            memcpy(recvMsg, recvBuf, bytesRecv);
            // 判断标志位和校验和，判断收到的ACK是否是想要的ACK
            if (isValidACK(recvMsg, lenInfo))
            {
                recvMsg->printMsg();
                cout << "Successful send file information" << endl;
                break;
            }
        }
        reTransmit(fileInfo);
    }
    initialSeqNum += fileInfo->header.getLength();
    delete fileInfo;
    delete recvMsg;

    int rounds = 0;
    clock_t fileStart = clock();
    sendFile(fileContent, rounds); // 传输文件
    clock_t fileEnd = clock();
    cout << "Transmit rounds: " << rounds << endl;
    cout << "Total time: " << (fileEnd - fileStart) / CLOCKS_PER_SEC << " s" << endl;
    cout << "Throughput: " << ((float)(fileSize + rounds * sizeof(Header))) / ((fileEnd - fileStart) / CLOCKS_PER_SEC) << " byte/s" << endl;
}

void closeConnect()
{
    Message *recvMsg = new Message();
    Message *msg = new Message();
    // 第一次挥手，发送FIN+ACK，第一次挥手消耗序列号
    sendFlag(msg, FIN + ACK, initialSeqNum, initialAckNum);
    start = clock();
    reTransmitCount = 0;
    // 收第二次挥手，没有收到就重发
    while (1)
    {
        int bytesRecv = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&serverAddr, &sockaddrSize);
        if (bytesRecv != SOCKET_ERROR)
        {
            recvMsg->reset();
            memcpy(recvMsg, recvBuf, bytesRecv);
            // 判断标志位和校验和
            if (recvMsg->header.getFlags() == FIN + ACK && recvMsg->isValid())
            {
                recvMsg->printMsg();
                cout << "Successful second handwaving" << endl;
                initialSeqNum++; // 确认完已成功发送后，序列号加1
                initialAckNum++; // 收到第二次握手后，ack+1
                sendFlag(msg, ACK, initialSeqNum, initialAckNum);
                break;
            }
        }
        // 超时重传
        reTransmit(msg);
    }
    while (1)
    {
        int bytesRecv = recvfrom(clientSock, recvBuf, sizeof(recvBuf), 0, (SOCKADDR *)&serverAddr, &sockaddrSize);
        if (bytesRecv != SOCKET_ERROR)
        {
            recvMsg->reset();
            memcpy(recvMsg, recvBuf, bytesRecv);
            start = clock();
            // 如果还能收到第二次挥手，则说明第三次挥手丢失了
            if (recvMsg->header.getFlags() == FIN + ACK && recvMsg->isValid())
            {
                HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                cout << "[Retransmission] ";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN);
                recvMsg->printMsg();
                sendFlag(msg, ACK, initialSeqNum, initialAckNum);
            }
        }
        // 等待一定时间
        if (static_cast<double>(clock() - start) / CLOCKS_PER_SEC >= waitTime)
        {
            break;
        }
    }
}

int main()
{
    prepareSocket();
    initialSeqNum = getRandom();

    shakeHands(); // 三次握手建立连接
    while (1)
    {
        char *fileContent = getFile(); // 获取要传输的文件
        if (fileContent)               // 传输文件信息
        {
            sendFileInfo(fileContent);
        }
        else
        {
            break;
        }
    }
    closeConnect();
    cout << "Bye";
    closesocket(clientSock);
    WSACleanup();
    return 0;
}
