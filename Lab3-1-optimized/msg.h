#ifndef __MSG_H__
#define __MSG_H__
#include <iostream>
#include <random>
#include <chrono>
#include <ctime>
#include <cstdlib>
using namespace std;

const double time_out = 0.1;
#define MAX_RETANSMIT 10
#define MAX_BUFFER_SIZE 6000
#define MSS 5120
#define SYN 4
#define ACK 2
#define FIN 1

struct Header
{
private:
	int seq = 0;				 // 32 bits, sequence number
	int ack = 0;				 // 32 bits, acknowledge number
	unsigned short checksum = 0; // 16 bits, checksum
	unsigned short length = 0;	 // 16 bits, data length
	unsigned short flags = 0;	 // 16 bits, SYN, ACK, FIN
public:
	bool isSYN() { return (flags & SYN) == SYN; }
	bool isACK() { return (flags & ACK) == ACK; }
	bool isFIN() { return (flags & FIN) == FIN; }
	int getFlags() { return flags; }
	int getAckNum() { return ack; }
	int getSeqNum() { return seq; }
	int getChecksum() { return checksum; }
	void setChecksum(unsigned short c) { checksum = c; }
	int getLength() { return length; }
	void setLength(int len) { length = len; }
	void setHeader(unsigned char flags, int seq, int ack)
	{
		this->flags = flags;
		this->seq = seq;
		this->ack = ack;
	}
};
struct Message
{
	struct Header header;
	char data[MSS];
	Message(){};
	void setChecksum() { header.setChecksum(calChecksum()); }
	unsigned short calChecksum();
	void printMsg(bool isSender = 0, bool printData = 0);
	bool isValid() { return calChecksum() == header.getChecksum(); };
	void reset() { memset(this, 0, sizeof(struct Message)); }
	void setData(char *d, int l)
	{
		memcpy(data, d, l);
		header.setLength(l);
	}
};

unsigned short Message::calChecksum()
{
	// 存储溢出
	unsigned int sum = 0;

	sum += (header.getSeqNum() >> 16) & 0xFFFF;
	sum += header.getSeqNum() & 0xFFFF;
	sum += (header.getAckNum() >> 16) & 0xFFFF;
	sum += header.getAckNum() & 0xFFFF;
	sum += header.getLength() & 0xFFFF;
	sum += header.getFlags() & 0xFFFF;

	int len = header.getLength();
	if (len != 0)
	{
		for (int i = 0; i < len - 1; i += 2)
		{
			sum += (static_cast<unsigned char>(data[i]) << 8) | static_cast<unsigned char>(data[i + 1]);
		}

		if (len % 2 == 1)
		{
			sum += static_cast<unsigned char>(data[len - 1]) << 8;
		}
	}

	// 回卷
	while (sum >> 16)
	{
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	unsigned short res = static_cast<unsigned short>(~sum);
	return res;
}

void Message::printMsg(bool isSender, bool printData)
{
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	if (isSender)
	{
		SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		cout << "[SEND] ";
	}
	else
	{
		SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
		cout << "[RECV] ";
	}

	cout << "Package "
		 << "[SYN:" << header.isSYN() << "] "
		 << "[ACK:" << header.isACK() << "] "
		 << "[FIN:" << header.isFIN() << "] "
		 << "[seq num:" << header.getSeqNum() << "] "
		 << "[ack num:" << header.getAckNum() << "] "
		 << "[CheckSum:" << header.getChecksum() << "] "
		 << "[Data Length:" << header.getLength() << "]";
	if (printData)
	{
		cout << "[Data]: ";
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN);
		for (int i = 0; i < header.getLength(); i++)
		{
			cout << data[i];
		}
	}
	cout << endl;

	SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_GREEN);
}


int getRandom()
{
	auto seed = chrono::system_clock::now().time_since_epoch().count(); // 使用当前时钟时间作为种子
	default_random_engine generator(seed);								// 初始化随机数引擎
	uniform_int_distribution<int> distribution(1, 10000);				// 均匀分布
	return distribution(generator);										// 生成随机数
}

#endif