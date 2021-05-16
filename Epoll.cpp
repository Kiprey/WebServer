#include <cassert>
#include <iostream>
#include <unistd.h>

#include "Epoll.h"

Epoll::Epoll(int flag) : epoll_fd_(-1)
{
    events_ = new epoll_event[MAX_EVENTS];
    create(flag);
}

Epoll::~Epoll()
{
    destroy();
    delete[] events_;
}

bool Epoll::isEpollValid()
{
    return epoll_fd_ >= 0;
}

bool Epoll::create(int flag)
{
    // 这里添加 epoll_fd_ < 0 的判断条件,防止重复 create.
    if(!isEpollValid()
        && ((epoll_fd_ = epoll_create1(flag)) == -1))
    {
        LOG(ERROR) << "Create Epoll fail! " << strerror(errno) << endl;
        return false;
    }
    return true;
}

bool Epoll::add(int fd, void* data, int event)
{
    if(isEpollValid())
    {
        epoll_event ep_event;
        ep_event.events = event;
        ep_event.data.ptr = data;

        return (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ep_event) != -1);
    }
    return false;
}

bool Epoll::modify(int fd, void* data, int event)
{
    if(isEpollValid())
    {
        epoll_event ep_event;
        ep_event.events = event;
        ep_event.data.ptr = data;
        
        return (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ep_event) != -1);
    }

    return false;
}

bool Epoll::del(int fd)
{
    if(isEpollValid())
        return (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) != -1);
    return false;
}

int Epoll::wait(int timeout)
{
    if(isEpollValid())
        return (epoll_wait(epoll_fd_, events_, MAX_EVENTS, timeout) != -1);
    return false;
}

void Epoll::destroy()
{
    // 如果文件描述符正常,则进行销毁
    if(isEpollValid())
        close(epoll_fd_);
    // 重置 epoll_fd_ 为无效描述符
    epoll_fd_ = -1;
}

epoll_event Epoll::getEvent(size_t index)
{
    assert(index < MAX_EVENTS);
    // 返回一个 const 指针
    return events_[index];
}
