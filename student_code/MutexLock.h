#ifndef MUTEXLOCK_H
#define MUTEXLOCK_H

#include <pthread.h>

/**
 * @brief MutexLock 将 pthread_mutex 封装成一个类, 
 *        这样做的好处是不用记住那些繁杂的 pthread 开头的函数使用方式
 */ 
class MutexLock
{
private:
    pthread_mutex_t mutex_;
public:
    MutexLock()     { pthread_mutex_init(&mutex_, nullptr); }
    ~MutexLock()    { pthread_mutex_destroy(&mutex_); }
    void lock()     { pthread_mutex_lock(&mutex_); }
    void unlock()   { pthread_mutex_unlock(&mutex_); }
    pthread_mutex_t* getMutex() { return &mutex_; };
};

/**
 * @brief MutexLockGuard 主要是为了自动获取锁/释放锁, 防止意外情况下忘记释放锁
 *        而且块状的锁定区域更容易让人理解代码
 */ 
class MutexLockGuard
{
private:
    MutexLock& lock_;
public:
    /**
     * @brief 声明 MutexLockGuard 时自动上锁
     * @param lock 待锁定的资源
     */
    MutexLockGuard(MutexLock& mutex) : lock_(mutex) { lock_.lock(); }
    /**
     * @brief 当前作用域结束时自动释放锁, 防止遗忘
     */ 
    ~MutexLockGuard() { lock_.unlock(); }
};

#endif