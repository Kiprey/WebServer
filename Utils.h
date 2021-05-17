#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <cstring>
#include <csignal>

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::ostream;

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
 * @note   该函数在错误时会生成 errno
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
 * @brief   read/recv的wrapper
 * @param   fd  源文件描述符
 * @param   buf 缓冲区地址
 * @param   len 目标读取的字节个数
 * @param   isBlock true 则以阻塞模式读取,否则以 非阻塞模式读取
 * @param   isRead  启用 read 函数
 * @return  成功读取的长度
 * @note    内部函数在错误时会生成 errno
 * @note    阻塞模式下,如果读取到任何数据则函数马上返回,如果没有读取到数据则阻塞
 *          非阻塞模式下,无论有没有读取到数据,都会马上返回
 * @note    默认情况下, 启用阻塞模式的recv函数(注意启用前必须已经设置 socket 为阻塞模式)
 * @note    recv函数与read函数的不同之处在于,recv专为socket而生,支持更多的错误处理
 */
ssize_t readn(int fd, void*buf, size_t len, 
                bool isBlock = true, bool isRead = false);

/**
 * @brief   write/send的wrapper
 * @param   fd  源文件描述符
 * @param   buf 缓冲区地址
 * @param   len 目标读取的字节个数
 * @param   isWrite 启用 write 函数
 * @return  成功读取的长度
 * @note    内部函数在错误时会生成 errno
 * @note    该函数将 **阻塞** 写入数据, 除非有其他错误发生
 */
ssize_t writen(int fd, void*buf, size_t len, bool isWrite = false);

/**
 * @brief 忽略 SIGPIPE信号
 * @note  当远程主机强迫关闭socket时,Server端会产生 SIGPIPE 信号
 *        但SIGPIPE信号默认关闭当前进程,因此在Server端处需要忽略该信号
 */
void handleSigpipe();

/**
 * @brief 将当前client_fd_对应的连接信息,以 LOG(INFO) 的形式输出
 * @param client_fd_ 待输出信息的 fd
 * @param prefix     输出信息的前缀,例如 "<prefix>: <othermsg>"
 */ 
void printConnectionStatus(int client_fd_, string prefix);

/**
 * @brief 将传入的字符串转义成终端可以直接显示的输出
 * @param str       待输出的字符串
 * @param MAXBUF    最长能输出的字符串长度
 * @return 转义后的字符串
 * @note  是将 '\r' 等无法在终端上显示的字符,转义成 "\r"字符串 输出
 */
string escapeStr(const string& str);

#endif