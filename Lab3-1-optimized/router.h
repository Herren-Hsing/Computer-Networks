#ifndef __ROUTER_H__
#define __ROUTER_H__
#include <winsock2.h>
#include <chrono>
#include <future>
#include <fstream>
#include <iomanip>
using namespace std;

#define missRate 20 // 每20个包丢1个，丢包率5%
#define delay 5     // 延时 5 毫秒
bool setMiss = true;
bool setDelay = true;

void logToFile(const string &logMessage, int counterValue)
{
    // 获取当前时间以生成日志时间戳
    auto now = chrono::system_clock::now();
    auto time = chrono::system_clock::to_time_t(now);

    ofstream logFile("routerLog.txt", ios::app); // 写入日志

    if (logFile.is_open())
    {
        logFile << put_time(localtime(&time), "%Y-%m-%d %X") << " - Counter: " << counterValue << " - " << logMessage << endl;
        logFile.close();
    }
    else
    {
        cerr << "Error opening log file!" << endl;
    }
}

void sendWithRegularLoss(SOCKET sock, const char *buffer, size_t length, int flags,
                         const struct sockaddr *destAddr, int addrlen)
{
    static int counter = 0;
    counter++;
    // 丢包
    if (setMiss && (counter % missRate == 0))
    {
        logToFile("Simulating packet loss. Packet not sent.", counter);
        return;
    }
    // 异步任务，延迟
    auto future = async(launch::async, [&sock, buffer, length, flags, destAddr, addrlen]()
                        {
    if (setDelay) {
        this_thread::sleep_for(chrono::milliseconds(delay));
        logToFile("Delay", counter);
    }
    sendto(sock, buffer, length, flags, destAddr, addrlen);
    logToFile("Successfully sent.", counter); });
}

#endif