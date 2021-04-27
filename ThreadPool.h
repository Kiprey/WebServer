#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <cassert>
#include <queue>

#include "Condition.h"
#include "MutexLock.h"

using namespace std;

class ThreadPool
{
public:
    // 线程池摧毁时,当前正在工作的线程是等待工作完成后退出(graceful) 还是直接退出(immediate)
    enum ShutdownMode { GRACEFUL_QUIT, IMMEDIATE_SHUTDOWN } ;
    /***
     * @brief   创建线程池
     * @param   threadNum       线程池线程个数
     * @param   shutdown_mode   当前线程池的摧毁方案
     * @param   maxQueueSize    线程池事件队列最大大小, 默认不设限制(-1)
     */
    ThreadPool( size_t threadNum, 
                ShutdownMode shutdown_mode = GRACEFUL_QUIT,
                size_t maxQueueSize = -1
    );
    
    /***
     * @brief   销毁线程池
     */
    ~ThreadPool();

    /***
     * @brief   将当前task加入至线程池中
     * @param   task 待处理的 task
     * @return  返回添加结果, true 表示添加成功, false 表示队列已满, 添加失败
     * @note    这里的 arguments 指针指向的对象,将 **不会** 在子线程内部事件执行完成后自动释放
     *          也就是说,外部调用者需要自己考虑到内存释放
     */ 
    bool appendTask(void (*function)(void*), void* arguments);

    // /**
    //  * @brief 声明一些获取线程池属性的方法.不管有没有用到,实现一下接口总是没错的.
    //  */ 
    // size_t getThreadNum()           { return threadNum_; }
    // size_t getWorkingThreadNum()    { return workingThreadNum_; }
    // size_t getIdleThreadNum()       { return idleThreadNum_; }
    // size_t getStartedThreadNum()    { return startedThreadNum_; }

private:
    /**
     * @brief 每个子线程所要执行的函数, 在该函数中轮询事件队列
     * @param pool 当前线程所属的线程池
     */ 
    static void* TaskForWorkerThreads_(void* arg);

    /***
     * 每个线程的基本事件单元
     */
    struct ThreadpoolTask
    {
        void (*function)(void*);
        void* arguments;
    };

    size_t threadNum_;                          // 线程个数

    // size_t workingThreadNum_;                   // 正在工作的线程个数
    // size_t idleThreadNum_;                      // 空闲线程个数
    // size_t startedThreadNum_;                   // 已经启动的线程个数,注意已经启动的线程分为 正在工作 和 空闲 两类

    size_t maxQueueSize_;                       // 事件队列最大长度,超出则停止添加新事件
    queue<ThreadpoolTask> task_queue_;          // 事件队列

    vector<pthread_t> threads_;                 // 线程的标识符

    MutexLock threadpool_mutex_;                // 线程池的锁,保证每次最多只能有一个线程正在操作该线程池
    Condition threadpool_cond_;                 // 线程池的条件变量,对于来新task时,唤醒空闲线程

    ShutdownMode  shutdown_mode_;               // 线程池析构时,剩余工作线程的处理方式

};

#endif