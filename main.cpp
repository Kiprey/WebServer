#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Epoll.h"
#include "HttpHandler.h"
#include "Log.h"
#include "ThreadPool.h"
#include "Utils.h"

using namespace std;

/**
 * @brief 处理新的连接
 * @param epoll     存放新连接的Epoll类实例 
 * @param listen_fd 新连接所对应的 listen 描述符
 */ 
void handleNewConnections(Epoll* epoll, int listen_fd, int* idle_fd)
{
    // 注意:可能会有很多个 connect 动作,但只会有一个 event
    sockaddr_in client_addr;
    socklen_t client_addr_len = 0;
    
    /**
     *  如果 
     *      1. accept 没有发生错误
     *      2. accppt 发生了 EINTR 错误
     *      3. accept 发生了 ECONNABORTED 错误(该错误是远程连接被中断)
     *  则重新循环. 其中第三点, 若发生了 aborted 错误,则继续循环接受下一个socket 的请求
     */
    for(;;) {
        int client_fd = accept4(listen_fd, (sockaddr*)&client_addr, &client_addr_len, 
                SOCK_NONBLOCK | SOCK_CLOEXEC);
        // accept 的错误处理
        if(client_fd == -1) {
            // 如果是因为一些无关的错误所阻断，则继续 accept
            if(errno == EINTR || errno == ECONNABORTED)
                continue;
            // 正常情况下,如果处理了所有的 accept后, errno == EAGAIN，则直接退出
            else if (errno == EAGAIN)
                break;
            // 如果由于文件描述符不够用了,则会返回 EMFILE，此时清空全部的尚未 accept 连接
            else if(errno == EMFILE) {
                WARN("No reliable pipes ! ");
                closeRemainingConnect(listen_fd, idle_fd);
                break;
            }
            // 如果是其他的错误，则输出信息
            else 
                ERROR("Accept Error! (%s)", strerror(errno));
        }
        // 如果 accept 正常
        else {
            /** 构建一个新的 HttpHandler,并放入 epoll 实例中
             *  注意这里使用了 ONESHOT, 每个套接字只会在 边缘触发,可读时处于就绪状态
             *  且每个套接字只会被一个线程处理
             *  NOTE: 每个 client_fd 只会在 HttpHandler 中被 close + 下面的 timer 异常处理中被关闭
             *        每个 client_handler 也只会在 setConnectionClosed 之后, 执行完 RunEventLoop 函数结束时被释放
             *        每个 Timer 在此处创建, 在 HttpHandler 中被释放
             *        可以看出,现在指针已经满天飞了 2333
             */
            Timer* timer = new Timer(TFD_NONBLOCK | TFD_CLOEXEC);
            // 如果timer创建失败,则清空当前所有尚未 accept 的连接，因为文件描述符满
            if(!timer->isValid())
            {
                delete timer;
                // 直接关闭，告诉远程这里放不下了
                close(client_fd);

                WARN("No reliable pipes ! ");
                closeRemainingConnect(listen_fd, idle_fd);
                break;
            }
            HttpHandler* client_handler = new HttpHandler(epoll, client_fd, timer);
            /**
             * @brief EPOLLRDHUP EPOLLHUP 不同点,前者是半关闭连接时出发,后者是完全关闭后触发
             * @ref tcp 源码 https://elixir.bootlin.com/linux/v4.19/source/net/ipv4/tcp.c#L524
             * @ref TCP: When is EPOLLHUP generated? https://stackoverflow.com/questions/52976152/tcp-when-is-epollhup-generated
             */ 
            bool ret1 = epoll->add(client_fd, client_handler->getClientEpollEvent(), client_handler->getClientTriggerCond());
            // 设置定时器以边缘-单次触发方式
            bool ret2 = epoll->add(timer->getFd(), client_handler->getTimerEpollEvent(), client_handler->getTimerTriggerCond());
            assert(ret1 && ret2);
            // 输出相关信息
            printConnectionStatus(client_fd, "-------->>>>> New Connection");
        }
    }
}

/**
 * @brief 处理旧的连接
 * @param epoll         被唤醒的 epoll
 * @param fd            被唤醒的文件描述符
 * @param thread_pool   目标线程池
 * @param event         待处理的事件
 */
void handleOldConnection(Epoll* epoll, int fd, ThreadPool* thread_pool, epoll_event* event)
{
    EpollEvent* curr_epoll_event = static_cast<EpollEvent*>(event->data.ptr);
    HttpHandler* handler = static_cast<HttpHandler*>(curr_epoll_event->ptr);
    // 处理一些错误事件
    int events_ = event->events;
    // 如果远程关闭了当前连接
    if ((events_ & EPOLLHUP) || (events_ & EPOLLRDHUP)) {
        INFO("Socket(%d) was closed by peer.", handler->getClientFd());
        // 当某个 handler 无法使用时,一定要销毁内存
        delete handler;
        // 之后重新开始遍历新的事件.
        return;
    }
    // 如果当前 socket / events_ 存在错误
    else if ((events_ & EPOLLERR) || !(events_ & EPOLLIN)) {
        ERROR("Socket(%d) error.", handler->getClientFd());
        // 当某个 handler 无法使用时,一定要销毁内存
        delete handler;
        // 之后重新开始遍历新的事件.
        return;
    }
    // 如果没有错误发生
    // 1. 如果是因为超时
    if(fd == handler->getTimer()->getFd())
    {
        INFO("-------->>>>> "
             "New Message: socket(%d) - timerfd(%d) timeout."
             " <<<<<--------",
             handler->getClientFd(), handler->getTimer()->getFd());
        /* 这里不像下面需要从epoll中关闭 timer fd
           因为 timer将会在HttpHandler的析构函数中从epoll内部删除 */
        // 删除 handler 实例
        delete handler;
    }
    // 2. 如果不是因为超时
    else 
    {
        // 则从epoll中关闭 timer, 防止条件竞争
        epoll->modify(handler->getTimer()->getFd(), nullptr, 0);
        // 并将其放入线程池中并行执行
        thread_pool->appendTask(
            // lambda 函数
            [](void* arg)
            {
                HttpHandler* handler = static_cast<HttpHandler*>(arg);

                printConnectionStatus(handler->getClientFd(), "-------->>>>> New Message");

                // 如果出现无法恢复的错误,则直接释放该实例以及对应的 client_fd
                if(!(handler->RunEventLoop()))
                    delete handler;
            }, 
            handler);
    }
}

int main(int argc, char* argv[])
{
    // 获取传入的参数
    if (argc < 2 || !isNumericStr(argv[1])) 
    {
        ERROR("usage: %s <port> [<www_dir>]", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    if(argc > 2)
        HttpHandler::setWWWPath(argv[2]);
    // 输出当前进程的 PID，便于调试
    INFO("PID: %d", getpid());
    // 忽略 SIGPIPE 信号
    handleSigpipe();
    // 创建线程池
    ThreadPool thread_pool(8);

    // 空闲 fd，用于关闭溢出的文件描述符
    int idle_fd = open("/dev/null", O_RDONLY | O_CLOEXEC); 
    int listen_fd = -1;
    if((listen_fd = socket_bind_and_listen(port)) == -1)
    {
        ERROR("Bind %d port failed ! (%s)", port, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // 声明一个 epoll 实例,该实例将在整个main函数结束时被释放
    Epoll epoll(EPOLL_CLOEXEC);
    assert(epoll.isEpollValid());
    // 将 listen_fd 添加进 epoll 实例
    EpollEvent* listen_epollevent = new EpollEvent{listen_fd, nullptr};
    epoll.add(listen_fd, listen_epollevent, EPOLLET | EPOLLIN);

    // 开始事件循环
    for(;;)
    {
        // 阻塞等待新的事件
        int event_num = epoll.wait(-1);
        // 如果报错
        if(event_num < 0)
        {
            // 表示该错误一定不是因为无效的 epoll 导致的
            assert(event_num != -2);
            // 如果只是中断,则直接重新循环
            if(errno == EINTR)
                continue;
            // 如果是其他异常,则输出信息并终止.
            else
                FATAL("epoll_wait fail! (%s)", strerror(errno));
        }
        // 如果什么也没读到,则可能是因为 signal 导致的.例如 SIGINT XD
        else if(event_num == 0)
            continue;
        
        // 遍历获取到的事件
        for(int i = 0; i < event_num; i++)
        {
            // 获取事件相关的信息
            epoll_event&& event = epoll.getEvent(static_cast<size_t>(i));
            EpollEvent* curr_epoll_event = static_cast<EpollEvent*>(event.data.ptr);
            
            int fd = curr_epoll_event->fd;
            
            // 如果当前文件描述符是 listen_fd, 则建立连接
            if(fd == listen_fd)
                handleNewConnections(&epoll, listen_fd, &idle_fd);
            else
                handleOldConnection(&epoll, fd, &thread_pool, &event);
        }
    }
    delete listen_epollevent;

    return 0;
}