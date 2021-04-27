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

#endif