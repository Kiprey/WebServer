#ifndef CONDITION_H
#define CONDITION_H

#include <cerrno>
#include <pthread.h>

#include "MutexLock.h"

/**
 * @brief 条件变量,主要用于多线程中的锁 
 *        与 MutexLock 一致,无需记住繁杂的函数名称
 *        条件变量主要是与mutex进行搭配,常用于资源分配相关的场景,
 *        例如当某个线程获取到锁以后,发现没有资源,则此时可以释放资源并等待条件变量
 * @note  注意: 使用条件变量时,必须上锁,防止出现多个线程共同使用条件变量
 */
class Condition
{
private:
    MutexLock& lock_;       // 目标 Mutex 互斥锁
    pthread_cond_t cond_;   // 条件变量
public:
    Condition(MutexLock& mutex) : lock_(mutex) { pthread_cond_init(&cond_, nullptr); }
    ~Condition()        { pthread_cond_destroy(&cond_); }
    void notify()       { pthread_cond_signal(&cond_); }
    void notifyAll()    { pthread_cond_broadcast(&cond_); }
    void wait()         { pthread_cond_wait(&cond_, lock_.getMutex()); }
    /**
     *  @brief  等待当前的条件变量一段时间
     *  @param  sec 等待的时间(单位:秒)
     *  @return 成功在时间内等待到则返回 true, 超时则返回 false
     */
    bool waitForSeconds(size_t sec)   
    {
        timespec abstime;
        // 获取当前系统真实时间
        clock_gettime(CLOCK_REALTIME, &abstime);
        abstime.tv_sec += (time_t)sec;
        return ETIMEDOUT != pthread_cond_timedwait(&cond_, lock_.getMutex(), &abstime);
    }
};

#endif