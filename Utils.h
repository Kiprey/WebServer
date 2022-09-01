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
 * @brief  绑定一个端口号并返回一个 fd
 * @param  port 目标端口号
 * @return 运行正常则返回 fd, 否则返回 -1
 * @note   该函数在错误时会生成 errno
 */
int socket_bind_and_listen(int port);

/**
 * @brief 设置传入的文件描述符为非阻塞模式
 * @param fd 传入的目标套接字
 * @return true表示设置成功, false表示设置失败
 * @note   fcntl函数在错误时会生成 errno
 */
bool setFdNoBlock(int fd);

/**
 * @brief 设置socket禁用 nagle算法
 * @param fd 目标套接字
 * @return true 表示设置成功, false 表示设置失败
 * @note   setsockopt函数在错误时会生成 errno
 */
bool setSocketNoDelay(int fd);

/**
 * @brief   非阻塞模式 read 的wrapper
 * @param   fd  源文件描述符
 * @param   buf 缓冲区地址
 * @param   len 目标读取的字节个数
 * @return  成功读取的长度
 * @note    内部函数在错误时会生成 errno
 * @note    非阻塞模式下,无论有没有读取到数据,都会马上返回
 * @note    readn 不能用于替代 recv
 *          因为当 readn 返回0时，调用者无法知道是连接关闭，还是当前暂时无数据可读
 */
ssize_t readn(int fd, void* buf, size_t len);

/**
 * @brief   write/send的wrapper
 * @param   fd  源文件描述符
 * @param   buf 缓冲区地址
 * @param   len 目标读取的字节个数
 * @param   isWrite 启用 write 函数
 * @return  成功读取的长度
 * @note    内部函数在错误时会生成 errno
 * @note    该函数将 **阻塞** 写入数据, 除非有其他错误发生
 * @note    writen 可以用于替代 send 进行阻塞写入操作
 */
ssize_t writen(int fd, const void* buf, size_t len, bool isWrite = false);

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
string escapeStr(const string& str, size_t MAXBUF);

/**
 * @brief 判断字符串是否全为数字
 * @return true 表示字符串全为数字, 否则返回false
 */
bool isNumericStr(string str);

/**
 * @brief 清空当前剩余尚未 accept 的连接
 * @param listen_fd 当前所监听的文件描述符
 * @param idle_fd 空闲的文件描述符
 * @return 返回清空的连接数量
 */ 
size_t closeRemainingConnect(int listen_fd, int* idle_fd);

/**
 * @brief 检测两个 path 是否包含从属关系，以防止目录穿越漏洞
 * @param root_dir 最外层的路径
 * @param child_dir 内层路径
 * @return 返回从属关系
 */ 
bool is_path_parent(const string& parent_path, const string& child_path);

#endif