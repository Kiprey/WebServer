#ifndef LOG_H
#define LOG_H

#include <cstdio>
#include <unistd.h>
#include <sys/syscall.h>

#include "MutexLock.h"

#define cRST "\x1b[0m"      // 终端红色字体代码
#define cLRD "\x1b[1;91m"   // 终端重置字体颜色代码
#define cYEL "\x1b[1;93m"   // 终端黄色字体代码
#define cBRI "\x1b[1;97m"   // 终端加粗白色字体代码
#define cLBL "\x1b[1;94m"   // 终端蓝色字体代码

// 全局日志输出锁
extern MutexLock global_log_lock;

#if 1
// 开启所有输出
#define INFO(x...) do { \
    MutexLockGuard log_guard(global_log_lock);    \
    fprintf(stdout, "(Thread %lx): ", syscall(SYS_gettid));  \
    fprintf(stdout, cLBL "[*] " cRST x); \
    fprintf(stdout, cRST "\n"); \
    fflush(stdout);    \
  } while (0)

#define WARN(x...) do { \
    MutexLockGuard log_guard(global_log_lock);    \
    fprintf(stderr, "(Thread %lx): ", syscall(SYS_gettid));  \
    fprintf(stderr, cYEL "[!] " cBRI "WARNING: " cRST x); \
    fprintf(stderr, cRST "\n");    \
    fflush(stderr);    \
  } while (0)

#define ERROR(x...) do { \
    MutexLockGuard log_guard(global_log_lock);    \
    fprintf(stderr, "(Thread %lx): ", syscall(SYS_gettid));  \
    fprintf(stderr, cLRD "[-] " cRST x); \
    fprintf(stderr, cRST "\n"); \
    fflush(stderr);    \
  } while (0)

#define FATAL(x...) do { \
    MutexLockGuard log_guard(global_log_lock);    \
    fprintf(stderr, "(Thread %lx): ", syscall(SYS_gettid));  \
    fprintf(stderr, cRST cLRD "[-] PROGRAM ABORT : " cBRI x); \
    fprintf(stderr, cLRD "\n         Location : " cRST "%s(), %s:%u\n\n", \
         __FUNCTION__, __FILE__, __LINE__); \
    fflush(stderr);    \
    abort(); \
  } while (0)

#else

// 关闭所有输出
#define INFO(x...) 
#define WARN(x...)
#define ERROR(x...) 
#define FATAL(x...) 

#endif

/**
 * @brief 调试用,表示某个代码区域不应该到达
 * @param x 可输出的信息
 */
#define UNREACHABLE(x) FATAL("UNREACHABLE CODE");

#endif