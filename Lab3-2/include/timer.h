#ifndef __TIMER_H__
#define __TIMER_H__

#include <thread>
#include <mutex>
using namespace std;

const double timeOut = 0.5;

// 计时器，用于客户端发送文件段时的超时处理
class Timer
{
private:
	bool isTiming;	   // 是否已经启动计时器
	bool timeout;	   // 是否超时
	clock_t startTime; // 计时开始时间
	thread timerThread;
	mutex mtx;
	bool stopTimerThread; // 是否终止计时线程，用于退出时结束线程
	void timerFunction(); // 计时线程处理函数

public:
	Timer() : isTiming(false), timeout(false), stopTimerThread(false)
	{
		timerThread = thread(&Timer::timerFunction, this); // 启动计时线程
	}

	~Timer()
	{
		stopTimer(); // 停止计时器线程

		if (timerThread.joinable())
		{
			timerThread.join();
		}
	}

	void start()
	{
		startTime = clock();
		isTiming = true;
	}

	void setStart(clock_t now)
	{
		startTime = now;
		isTiming = true;
	}

	void stop()
	{
		isTiming = false;
		lock_guard<mutex> lock(mtx);
		timeout = false;
	}
	void stopTimer() { stopTimerThread = true; }
	bool isTimeout() { return timeout; }
};

void Timer::timerFunction()
{
	while (!stopTimerThread)
	{
		if (isTiming)
		{
			clock_t currentTime = clock();
			double duration = static_cast<double>(currentTime - startTime) / CLOCKS_PER_SEC;

			// 超过预定的超时时间
			if (duration >= timeOut)
			{
				lock_guard<mutex> lock(mtx);
				timeout = true;
			}
			else
			{
				lock_guard<mutex> lock(mtx);
				timeout = false;
			}
		}
	}
}

#endif