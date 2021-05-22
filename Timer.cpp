#include <unistd.h>

#include "Timer.h"
#include "Utils.h"

Timer::Timer(int flag, time_t sec, long nsec) : timer_fd_(-1)
{
    create(flag);
    setTime(sec, nsec);
}

Timer::~Timer()
{
    cancel();
    destroy();
}

int Timer::getFd()
{
    return timer_fd_;
}

void Timer::setFd(int fd)
{
    timer_fd_ = fd;
}

bool Timer::isValid()
{
    return timer_fd_ >= 0;
}

bool Timer::create(int flag)
{
    // 这里使用 CLOCK_BOOTTIME **相对时间**, 排除了系统时间与系统休眠时间的干扰
    if(!isValid() && ((timer_fd_ = timerfd_create(CLOCK_BOOTTIME, flag)) == -1))
    {
        LOG(ERROR) << "Create Timer fail! " << strerror(errno) << endl;
        return false;
    }
    return true;
}

bool Timer::setTime(time_t sec, long nsec)
{
    struct itimerspec timerspec;
    // 初始化为0
    memset(&timerspec, 0, sizeof(timerspec));
    // 设置超时事件,注意该定时器只是一次性的, 因为itimerspec.interval两个字段全为0
    timerspec.it_value.tv_nsec = nsec;
    timerspec.it_value.tv_sec = sec;
    if(!isValid() || (timerfd_settime(timer_fd_, 0, &timerspec, nullptr) == -1))
    {
        LOG(ERROR) << "Timer setTime fail! " << strerror(errno) << endl;
        return false;
    }
    return true;
}

bool Timer::cancel()
{
    // 设置itimerspec的value两个字段全为0时则表示取消
    return setTime(0, 0);
}

void Timer::destroy()
{
    if(isValid())
        close(timer_fd_);
    timer_fd_ = -1;
}

timespec Timer::getNextTimeout()
{
    itimerspec nextTime;
    if(!isValid() || (timerfd_gettime(timer_fd_, &nextTime) == -1))
    {
        LOG(ERROR) << "Timer getNextTimeout fail! " << strerror(errno) << endl;
        timespec ret;
        ret.tv_nsec = ret.tv_sec = -1;
        return ret;
    }
    return nextTime.it_interval;
}