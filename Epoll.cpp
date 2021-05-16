#include <cassert>
#include <iostream>
#include <unistd.h>

#include "Epoll.h"

Epoll::Epoll() : epoll_fd_(-1)
{
    events_ = new epoll_event[MAX_EVENTS];
}

Epoll::~Epoll()
{
    destroy();
    delete[] events_;
}

bool Epoll::create(int flag)
{
    // 这里添加 epoll_fd_ < 0 的判断条件,防止重复 create.
    if((epoll_fd_ < 0) 
        && ((epoll_fd_ = epoll_create1(flag)) == -1))
    {
        LOG(ERROR) << "Create Epoll fail! " << strerror(errno) << endl;
        return false;
    }
    return true;
}

bool Epoll::add(int fd, void* data, int event)
{
    assert(epoll_fd_ >= 0);

    epoll_event ep_event;
    ep_event.events = event;
    ep_event.data.ptr = data;

    return (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ep_event) != -1);
}

bool Epoll::modify(int fd, void* data, int event)
{
    assert(epoll_fd_ >= 0);

    epoll_event ep_event;
    ep_event.events = event;
    ep_event.data.ptr = data;
    
    return (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ep_event) != -1);
}

bool Epoll::del(int fd)
{
    assert(epoll_fd_ >= 0);
    return (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) != -1);
}

int Epoll::wait(int timeout)
{
    assert(epoll_fd_ >= 0);
    return (epoll_wait(epoll_fd_, events_, MAX_EVENTS, timeout) != -1);
}

void Epoll::destroy()
{
    // 如果文件描述符正常,则进行销毁
    if(epoll_fd_ >= 0)
        close(epoll_fd_);
}

epoll_event Epoll::getEvent(size_t index)
{
    assert(index < MAX_EVENTS);
    // 返回一个 const 指针
    return events_[index];
}
