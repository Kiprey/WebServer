#ifndef UTILS_H
#define UTILS_H

#include <iostream>

/**
 * @brief   输出信息相关宏定义与函数
 *          使用 `LOG(INFO) << "msg";` 形式以执行信息输出.
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
 * @return 0表示设置成功, -1表示设置失败
 */
int setSocketNoBlock(int fd);

/**
 * @brief   read的多线程版本
 * @param   fd  源文件描述符
 * @param   buf 缓冲区地址
 * @param   len 目标读取的字节个数
 * @return  成功读取的长度
 */
ssize_t readn(int fd, void*buf, size_t len);

/**
 * @brief   write的多线程版本
 * @param   fd  源文件描述符
 * @param   buf 缓冲区地址
 * @param   len 目标读取的字节个数
 * @return  成功读取的长度
 */
ssize_t writen(int fd, void*buf, size_t len);

#endif