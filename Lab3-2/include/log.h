#ifndef __LOG_H__
#define __LOG_H__

#include <iostream>
#include <fstream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdarg>
#include <iomanip>
using namespace std;

class Logger
{
private:
    thread logThread;       // 日志线程
    queue<string> logQueue; // 日志消息队列
    mutex mtx;
    void async_write(); // 异步写日志的线程函数
public:
    bool stopLog; // 是否停止日志线程
    ofstream logFile;
    condition_variable condition;

    // 初始化日志文件和日志线程
    Logger(const string &logFileName) : stopLog(false)
    {
        logFile.open(logFileName, ios::trunc);
        logThread = thread(&Logger::async_write, this);
    }

    // 停止日志线程并关闭日志文件
    ~Logger()
    {
        stopLog = true;
        condition.notify_one(); // 通知等待中的线程停止
        logThread.join();       // 等待日志线程结束
        logFile.close();
    }

    // 将消息加入到日志队列
    void log(const string &message)
    {
        lock_guard<mutex> lock(mtx);
        logQueue.push(message);
        condition.notify_one(); // 通知日志线程有新的数据
    }

    void log(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
        tm tm = *localtime(&now);
        ostringstream timestamp;
        timestamp << put_time(&tm, "%Y-%m-%d %H:%M:%S");
        string message = "[" + timestamp.str() + "] " + buffer;

        log(message);
    }
};

void Logger::async_write()
{
    while (true)
    {
        unique_lock<mutex> lock(mtx);

        // 执行条件：有数据或者需要停止线程；否则就一直等待
        condition.wait(lock, [this]
                       { return !logQueue.empty() || stopLog; });

        // 检查是否是停止日志的时候
        if (stopLog && logQueue.empty())
        {
            break;
        }

        // 从队列中取出最前面的消息
        string message = logQueue.front();
        logQueue.pop();
        lock.unlock();

        // 写入日志文件并输出
        logFile << message << endl;
        cout << message << endl;
    }
}
#endif
