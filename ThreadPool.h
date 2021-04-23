#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <cassert>
#include <pthread.h>
#include <queue>

using namespace std;

class ThreadPool
{
private:
    /***
     * 每个线程的基本事件单元
     */
    struct Threadpool_task_t
    {
        void (*function)(void*);
        void* arguments;
    };

    size_t minThreadNum_;                       // 最小的线程个数
    size_t maxThreadNum_;                       // 最大的线程个数

    size_t workingThreadNum_;                   // 正在工作的线程个数
    size_t idleThreadNum_;                      // 空闲线程个数
    size_t startedThreadNum_;                   // 已经启动的线程个数,注意已经启动的线程分为 正在工作 和 空闲 两类

    size_t maxQueueSize_;                       // 事件队列最大长度,超出则停止添加新事件
    queue<Threadpool_task_t> task_queue_;       // 事件队列

    pthread_mutex_t threadpool_mutex;           // 线程池的锁,保证每次最多只能有一个线程正在操作该线程池
    pthread_cond_t  threadpool_cond;            // 线程池的条件变量,对于来新task时,唤醒空闲线程

public:
    /***
     * @brief   创建线程池
     * @param   minThreadNum 线程池最低线程个数
     * @param   maxThreadNum 线程池最多线程个数
     * @param   maxQueueSize 线程池事件队列最大大小, 默认不设限制(-1)
     */
    ThreadPool(size_t minThreadNum, size_t maxThreadNum, size_t maxQueueSize = -1);
    
    /***
     * @brief   销毁线程池
     */
    ~ThreadPool();

    /***
     * @brief   将当前task加入至线程池中
     * @param   task 待处理的 task
     * @return  返回添加结果, true 表示添加成功, false 表示队列已满, 添加失败
     * @note    这里的 arguments 由调用者释放 
     * @todo    该如何妥善处理这里的 argument 释放问题,指针生命周期问题
     */ 
    bool appendTask(void (*function)(void*), void* arguments);

    /**
     * @brief 每个子线程所要执行的函数, 在该函数中轮询事件队列
     * @param pool 当前线程所属的线程池
     */ 
    static void Thread_Workers(ThreadPool* pool);

};

#endif