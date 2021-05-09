#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <cstring>
#include <csignal>

/**
 * @brief   输出信息相关宏定义与函数
 *          使用 `LOG(INFO) << "msg";` 形式以执行信息输出.
 * @note    注意: 该宏功能尚未完备,多线程下使用LOG宏将会导致输出数据混杂
 */
#define INFO    1           /* 普通输出 */
#define ERROR   2           /* 错误输出 */
#define LOG(x)  logmsg(x)   /* 调用输出函数 */
std::ostream& logmsg(int flag);


/**
 * @brief  绑定一个端口号并返回一个 fd
 * @param  port 目标端口号
 * @return 运行正常则返回 fd, 否则返回 -1
 */
int socket_bind_and_listen(int port);

/**
 * @brief 设置传入的socket为非阻塞模式
 * @param fd 传入的目标套接字
 * @return true表示设置成功, false表示设置失败
 * @note   fcntl函数在错误时会生成 errno
 */
bool setSocketNoBlock(int fd);

/**
 * @brief 设置socket禁用 nagle算法
 * @param fd 目标套接字
 * @return true 表示设置成功, false 表示设置失败
 * @note   setsockopt函数在错误时会生成 errno
 */
bool setSocketNoDelay(int fd);

/**
 * @brief   read的wrapper
 * @param   fd  源文件描述符
 * @param   buf 缓冲区地址
 * @param   len 目标读取的字节个数
 * @return  成功读取的长度
 * @note    内部函数在错误时会生成 errno
 */
ssize_t readn(int fd, void*buf, size_t len);

/**
 * @brief   write的wrapper
 * @param   fd  源文件描述符
 * @param   buf 缓冲区地址
 * @param   len 目标读取的字节个数
 * @return  成功读取的长度
 * @note    内部函数在错误时会生成 errno
 */
ssize_t writen(int fd, void*buf, size_t len);

/**
 * @brief 忽略 SIGPIPE信号
 * @note  当远程主机强迫关闭socket时,Server端会产生 SIGPIPE 信号
 *        但SIGPIPE信号默认关闭当前进程,因此在Server端处需要忽略该信号
 */
void handleSigpipe();

#endif