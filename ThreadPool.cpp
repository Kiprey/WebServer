#include "ThreadPool.h"

ThreadPool::ThreadPool(int minThreadNum, int maxThreadNum)
{
    minThreadNum_ = minThreadNum;
    maxThreadNum_ = maxThreadNum;
    // 开始循环创建线程
}