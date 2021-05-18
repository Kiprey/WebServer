#ifndef EPOLL_H
#define EPOLL_H
#include <sys/epoll.h>

#include "Utils.h"

using namespace std;

class Epoll
{
public:
    /**
     * @brief 默认声明该 Epoll 类实例是自动分配 epoll实例
     */
    Epoll(int flag = 0);
    ~Epoll();

    /**
     * @brief 确定当前epoll文件描述符是否有效
     * @return 有效则返回 true, 无效则返回 false
     */ 
    bool isEpollValid();
    
    /**
     * @brief 创建一个 epoll 实例
     * @param flag 传递给 epoll_create1 的标志.可以设置 0 或 EPOLL_CLOEXEC.
     * @return 创建成功返回true,创建失败返回false
     * @note 该函数调用的内部函数在错误时会设置 errno
     * @note 必须在使用该类成员中的其他函数之前, 确保该函数成功调用 
     */ 
    bool create(int flag = 0);

    /**
     * @brief 向工作列表中添加条目
     * @param fd 目标文件描述符
     * @param data 添加到事件的数据指针
     * @param event 触发何种事件类型时进入就绪(epoll_event.events)
     * @return 成功则返回true, 失败则返回 false
     * @note 该函数调用的内部函数在错误时会设置 errno
     */ 
    bool add(int fd, void* data, int event);

    /**
     * @brief 修改工作列表中的条目
     * @param fd 目标文件描述符
     * @param data 添加到事件的数据指针
     * @param event 触发何种事件类型时进入就绪(epoll_event.events)
     * @return 成功则返回true, 失败则返回 false
     * @note 该函数调用的内部函数在错误时会设置 errno
     */ 
    bool modify(int fd, void* data, int event);

    /**
     * @brief 删除工作列表中的特定条目
     * @param fd 目标文件描述符
     * @return 成功则返回true, 失败则返回 false
     * @note 该函数调用的内部函数在错误时会设置 errno
     */ 
    bool del(int fd);

    /**
     * @brief 等待事件到来
     * @param timeout 最大超时时间,单位毫秒. -1 则设置永久等待
     * @return 返回处于就绪状态的文件描述符个数,出错时返回 -1
     * @note 该函数调用的内部函数在错误时会设置 errno
     */ 
    int wait(int timeout); 

    /**
     * @brief 释放当前 epoll 实例
     */ 
    void destroy();

    /**
     * @brief 获取事件数组中的对应事件
     * @param index 事件数组中的索引
     * @note  调用者必须确保 index 不能越界.
     */
    epoll_event getEvent(size_t index);

private:
    static const size_t MAX_EVENTS = 1024;

    int epoll_fd_;
    epoll_event events_[MAX_EVENTS];
};

#endif