#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t minThreadNum, size_t maxThreadNum, 
                    ShutdownMode shutdown_mode, size_t maxQueueSize)
{
    assert(minThreadNum <= maxThreadNum);

    threadpool_cond_(threadpool_mutex_);

    shutdown_mode_ = shutdown_mode;
    minThreadNum_ = minThreadNum;
    maxThreadNum_ = maxThreadNum;
    
    // 开始循环创建线程

}

ThreadPool::~ThreadPool()
{

}

bool ThreadPool::appendTask(void (*function)(void*), void* arguments)
{
    if(task_queue_.size() > maxQueueSize_)
        return false;
    else
    {
        ThreadpoolTask task = { function, arguments };
        task_queue_.push(task);
        return true;
    }
}

void ThreadPool::Thread_Workers(ThreadPool* pool)
{
    // 启动当前线程
    
    // 对于子线程来说,事件循环开始
    for(;;)
    {

    }
    // 退出当前线程
}