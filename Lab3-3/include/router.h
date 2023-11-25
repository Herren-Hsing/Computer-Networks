#ifndef __ROUTER_H__
#define __ROUTER_H__
#include <winsock2.h>
#include <chrono>
#include <future>
#include <fstream>
#include <iomanip>
#include <random>
using namespace std;

bool setMiss = true;    // 是否开启丢包
bool setDelay = true;   // 是否开启延迟
double missRate = 0.05; // 丢包率5%
int delay = 5;          // 延时 5 毫秒

random_device rd;
mt19937 gen(rd());
uniform_real_distribution<> dis(0.0, 1.0); // 生成范围在 [0.0, 1.0) 之间的随机数

void setValue(double a, int b)
{
    if (a == 0)
    {
        setMiss = false;
    }
    if (b == 0)
    {
        setDelay = false;
    }
    missRate = a;
    delay = b;
}

void logToFile(const string &logMessage, int counterValue)
{
    // 获取当前时间以生成日志时间戳
    auto now = chrono::system_clock::now();
    auto time = chrono::system_clock::to_time_t(now);

    ofstream logFile("log/routerLog.txt", ios::app); // 写入日志文件

    if (logFile.is_open())
    {
        logFile << put_time(localtime(&time), "%Y-%m-%d %X") << " - Counter: " << counterValue << " - " << logMessage << endl;
        logFile.close();
    }
}

// 调用sendto，人为增加调用的延时和丢失，如果丢失返回true
bool sendWithRegularLoss(SOCKET sock, const char *buffer, size_t length, int flags,
                         const struct sockaddr *destAddr, int addrlen)
{
    static int counter = 0;
    counter++;
    // 丢包
    if (setMiss && (dis(gen) < missRate))
    {
        logToFile("Simulating packet loss. Packet not sent.", counter); // 日志输出
        return true;
    }
    // 异步任务，延迟
    auto future = async(launch::async, [&sock, buffer, length, flags, destAddr, addrlen]()
                        {
    if (setDelay) {
        this_thread::sleep_for(chrono::milliseconds(delay)); // 等待延时时间
        logToFile("Delay", counter);//日志输出
    }
    sendto(sock, buffer, length, flags, destAddr, addrlen);
    logToFile("Successfully sent.", counter); }); // 日志输出
    return false;
}

#endif