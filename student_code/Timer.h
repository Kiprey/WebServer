#ifndef TIMER_H
#define TIMER_H

#include <sys/timerfd.h> 

class Timer
{
private:
    int timer_fd_;
public:
    /**
     * @brief 初始时自动创建 timer fd,释放时自动释放timer fd
     * @param flag 用以设置 timer fd 的属性, TFD_NONBLOCK / TFD_NONBLOCK
     * @param sec 超时时间,单位秒
     * @param nsec 超时时间,单位纳秒
     */ 
    Timer(int flag = 0, time_t sec = 0, long nsec = 0);
    ~Timer();

    /**
     * @brief 获取与设置当前Timer的文件描述符
     */
    int getFd();
    void setFd(int fd);

    /**
     * @brief 判断当前timer fd是否可用
     * @return true表示可用,false表示不可用
     */ 
    bool isValid();

    /**
     * @brief 主动创建一个 timer fd,如果原先fd有效则不会重复创建
     * @param flag 用以设置 timer fd 的属性, TFD_NONBLOCK / TFD_NONBLOCK
     * @note 该函数调用的内部函数在错误时会设置 errno
     * @note 必须在使用该类成员中的其他函数之前, 确保该函数成功调用 
     */
    bool create(int flag = 0);

    /**
     * @brief 设置一次性定时器
     * @param sec 表示定时器的秒级时间
     * @param nsec 表示定时器的纳秒级时间
     * @return true 表示设置正常,false表示设置失败
     * @note 若sec和nsec同时为0 ,则表示关闭定时器. sec & nsec 共同表示定时器的超时时间
     * @note 该函数调用的内部函数在错误时会设置 errno
     */
    bool setTime(time_t sec, long nsec);

    /**
     * @brief 取消定时器, 即setTime(0 ,0)
     * @return true 表示设置正常,false表示设置失败
     * @note 该函数调用的内部函数在错误时会设置 errno
     */
    bool cancel();

    /**
     * @brief 销毁timer fd
     */ 
    void destroy();

    /**
     * @brief 获取当前定时器距离下一次超时的时间
     * @return 返回timespec结构的时间
     * @note 若该函数执行失败,则返回的timespec结构体中,两个字段均为负数
     * @note 该函数调用的内部函数在错误时会设置 errno
     */ 
    timespec getNextTimeout(); 
};

#endif