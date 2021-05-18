#include "ThreadPool.h"
#include "Utils.h"

ThreadPool::ThreadPool(size_t threadNum, ShutdownMode shutdown_mode, size_t maxQueueSize)
        : threadNum_(threadNum),
          maxQueueSize_(maxQueueSize), 
          // 使用 类成员变量 threadpool_mutex_ 来初始化 threadpool_cond_
          threadpool_cond_(threadpool_mutex_), 
          shutdown_mode_(shutdown_mode)
{
    // 开始循环创建线程 
    while(threads_.size() < threadNum_)
    {
        pthread_t thread;
        // 如果线程创建成功,则将其压入栈内存中
        if(!pthread_create(&thread, nullptr, TaskForWorkerThreads_, this))
        {
            threads_.push_back(thread);
            // // 注意这里只修改已启动的线程数量
            // startedThreadNum_++;
        }
    }
}

ThreadPool::~ThreadPool()
{
    // 向任务队列中添加退出线程事件,注意上锁
    // 注意在 cond 使用之前一定要上 mutex
    {
        // 操作 task_queue_ 时一定要上锁
        MutexLockGuard guard(threadpool_mutex_);
        // 如果需要立即关闭当前的线程池,则
        if(shutdown_mode_ == IMMEDIATE_SHUTDOWN)
            // 先将当前队列清空
            while(!task_queue_.empty())
                task_queue_.pop();

        // 往任务队列中添加退出线程任务
        for(size_t i = 0; i < threadNum_; i++)
        {
            auto pthreadExit = [](void*) { pthread_exit(0); };
            ThreadpoolTask task = { pthreadExit, nullptr };
            task_queue_.push(task);
        }
        // 唤醒所有线程以执行退出操作
        threadpool_cond_.notifyAll();
    }
    for(size_t i = 0; i < threadNum_; i++)
    {
        // 回收线程资源
        pthread_join(threads_[i], nullptr);
    }
}

bool ThreadPool::appendTask(void (*function)(void*), void* arguments)
{
    // 由于会操作事件队列,因此需要上锁
    MutexLockGuard guard(threadpool_mutex_);
    // 如果队列长度过长,则将当前task丢弃
    if(task_queue_.size() > maxQueueSize_)
        return false;
    else
    {
        // 添加task至列表中
        ThreadpoolTask task = { function, arguments };
        task_queue_.push(task);
        // 每当有新事件进入之时,只唤醒一个等待线程
        threadpool_cond_.notify();
        return true;
    }
}

void* ThreadPool::TaskForWorkerThreads_(void* arg)
{
    ThreadPool* pool = (ThreadPool*)arg;
    // 启动当前线程
    ThreadpoolTask task;
    // 对于子线程来说,事件循环开始
    for(;;)
    {
        // 首先获取事件
        {
            // 获取事件时需要上个锁
            MutexLockGuard guard(pool->threadpool_mutex_);

            /** 
             * 如果好不容易获得到锁了,但是没有事件可以执行
             * 则陷入沉睡,释放锁,并等待唤醒
             * NOTE: 注意, pthread_cond_signal 会唤醒至少一个线程
             *       也就是说,可能存在被唤醒的线程仍然没有事件处理的情况
             *       这时只需循环wait即可.
             */ 
            while(pool->task_queue_.size() == 0)
                pool->threadpool_cond_.wait();
            // 唤醒后一定有事件
            assert(pool->task_queue_.size() != 0);
            task = pool->task_queue_.front();
            pool->task_queue_.pop();
        }
        // 执行事件
        (task.function)(task.arguments);
    }
    // 注意: UNREACHABLE, 控制流不可能会到达此处
    // 因为线程的退出不会走这条控制流,而是执行退出事件
    UNREACHABLE();
    return nullptr;
}