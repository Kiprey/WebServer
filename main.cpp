#include <iostream>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Epoll.h"
#include "HttpHandler.h"
#include "ThreadPool.h"
#include "Utils.h"

using namespace std;

/**
 * @brief 处理新的连接
 * @param epoll     存放新连接的Epoll类实例 
 * @param listen_fd 新连接所对应的 listen 描述符
 */ 
void handlerNewConnections(Epoll* epoll, int listen_fd)
{
    // 注意:可能会有很多个 connect 动作,但只会有一个 event
    sockaddr_in client_addr;
    socklen_t client_addr_len = 0;
    int client_fd = -1;
    
    /**
     *  如果 
     *      1. accept 没有发生错误
     *      2. accppt 发生了 EINTR 错误
     *      3. accept 发生了 ECONNABORTED 错误(该错误是远程连接被中断)
     *  则重新循环. 其中第三点, 若发生了 aborted 错误,则继续循环接受下一个socket 的请求
     */
    do{
        while((client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_addr_len)) != -1)
        {
            // 设置 client_fd 非阻塞
            if(!setSocketNoBlock(client_fd))
            {
                LOG(ERROR) << "Can not set socket " << client_fd << " No Block ! " 
                            << strerror(errno) << endl;
                // 如果报错,注意关闭 client_fd
                /// TODO: 关闭 client_fd 之前, 发送一个 500 错误?
                close(client_fd);
                continue;
            }
            /** 构建一个新的 HttpHandler,并放入 epoll 实例中
             *  注意这里使用了 ONESHOT, 每个套接字只会在 边缘触发,可读时处于就绪状态
             *  且每个套接字只会被一个线程处理
             *  NOTE: 每个 client_fd 只会在 HttpHandler 中被 close
             *        每个 client_handler 也只会在 setConnectionClosed 之后, 执行完 RunEventLoop 函数结束时被释放
             *        可以看出,现在指针已经满天飞了 2333
             */
            HttpHandler* client_handler = new HttpHandler(epoll, client_fd);
            epoll->add(client_fd, client_handler, EPOLLET | EPOLLIN | EPOLLONESHOT);
            // 输出相关信息
            printConnectionStatus(client_fd, "New Connection");
        }
    }while(errno == EINTR || errno == ECONNABORTED);
    
    // accept 的错误处理
    if(errno != EAGAIN)
        LOG(ERROR) << "Accept Error! " << strerror(errno) << endl;
}

/**
 * @brief 处理旧的连接
 * @param thread_pool   目标线程池
 * @param event         待处理的事件
 */
void handlerOldConnection(ThreadPool* thread_pool, epoll_event* event)
{
    HttpHandler* handler = static_cast<HttpHandler*>(event->data.ptr);
    // 处理一些错误事件
    int events_ = event->events;
    // 如果存在错误,或者不是因为 read 事件而被唤醒
    if((events_ & EPOLLERR) || (events_ & EPOLLHUP) || !(events_ & EPOLLIN))
    {
        LOG(ERROR) << "Error events(" << events_ << ")" << endl;
        // 当某个 handler 无法使用时,一定要销毁内存
        delete handler;
        // 之后重新开始遍历新的事件.
        return;
    }
    // 如果没有错误发生,则将其放入线程池中并行执行
    auto handlerConnect = [](void* arg)
    {
        HttpHandler* handler = static_cast<HttpHandler*>(arg);

        int client_fd = handler->getClientFd();
        printConnectionStatus(client_fd, "New Message");

        // 如果出现无法恢复的错误,则直接释放该实例以及对应的 client_fd
        if(!(handler->RunEventLoop()))
            delete handler;
    };
    
    thread_pool->appendTask(handlerConnect, handler);
}

int main(int argc, char* argv[])
{
    // 获取传入的参数
    if (argc < 2 || !isNumericStr(argv[1])) 
    {
        LOG(ERROR) << "usage: " << argv[0] << " <port> [<www_dir>]" << endl;
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    if(argc > 2)
        HttpHandler::setWWWPath(argv[2]);

    // 忽略 SIGPIPE 信号
    handleSigpipe();
    // 创建线程池
    ThreadPool thread_pool(8);

    int listen_fd = -1;
    if((listen_fd = socket_bind_and_listen(port)) == -1)
    {
        LOG(ERROR) << "Bind " << port << " port failed ! " 
                   << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }

    // 设置 listen_fd 非阻塞
    if(!setSocketNoBlock(listen_fd))
    {
        LOG(ERROR) << "Can not set socket " << listen_fd << " No Block ! " 
                    << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    
    // 声明一个 epoll 实例,该实例将在整个main函数结束时被释放
    Epoll epoll;
    assert(epoll.isEpollValid());
    /**
     * 将 listen_fd 添加进 epoll 实例
     * 这里构造了一个 HttpHandler 给 listen_fd.
     * NOTE: 监听套接字实际上并没有处理 Http 报文
     *       该HttpHandler的作用 **只是存放 listen_fd** .
     *       listen_handler 将会在main函数结束前被释放(尽管main函数永不结束)
     */
    HttpHandler* listen_handler = new HttpHandler(&epoll, listen_fd);
    epoll.add(listen_fd, listen_handler, EPOLLET | EPOLLIN);

    // 开始事件循环
    for(;;)
    {
        // 阻塞等待新的事件
        int event_num = epoll.wait(-1);
        // 如果报错
        if(event_num < 0)
        {
            // 如果只是中断,则直接重新循环
            if(errno == EINTR)
                continue;
            // 如果是其他异常,则输出信息并终止.
            else
            {
                LOG(ERROR) << "epoll_wait fail! " << strerror(errno) 
                           << ". abort!" << endl;
                abort();
            }
        }
        // 如果什么也没读到,则可能是因为 signal 导致的.例如 SIGINT XD
        else if(event_num == 0)
            continue;
        
        // 遍历获取到的事件
        for(int i = 0; i < event_num; i++)
        {
            // 获取事件相关的信息
            epoll_event&& event = epoll.getEvent(static_cast<size_t>(i));
            HttpHandler* handler = static_cast<HttpHandler*>(event.data.ptr);
            int fd = handler->getClientFd();
            
            // 如果当前文件描述符是 listen_fd, 则建立连接
            if(fd == listen_fd)
                handlerNewConnections(&epoll, listen_fd);
            else
                handlerOldConnection(&thread_pool, &event);
        }
    }
    delete listen_handler;

    return 0;
}