#include <iostream>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "HttpHandler.h"
#include "ThreadPool.h"
#include "Utils.h"

using namespace std;

void handlerConnect(void* arg)
{
    int* fd_ptr = (int*)arg;
    int client_fd = *fd_ptr;
    delete fd_ptr;

    if(client_fd < 0)
    {
        LOG(ERROR) << "client_fd error in handlerConnect" << endl;
        return;
    }
    HttpHandler handler(client_fd);
    // 目前先做无连续连接的方式
    handler.RunEventLoop();
    close(client_fd);
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        LOG(ERROR) << "usage: " << argv[0] << " <port>" << endl;
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);

    // 忽略 SIGPIPE 信号
    handleSigpipe();

    int listen_fd = -1;
    if((listen_fd = socket_bind_and_listen(port)) == -1)
    {
        LOG(ERROR) << "Bind " << port << " port failed !" << endl;
        exit(EXIT_FAILURE);
    }
    // 创建线程池
    ThreadPool thread_pool(16);
    // 开始事件循环
    for(;;)
    {
        sockaddr_in client_addr;
        socklen_t client_addr_len = 0;
        int client_fd = -1;
        if((client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_addr_len)) == -1)
        {
            LOG(ERROR) << "connect " << ntohl(client_addr.sin_addr.s_addr) << " failed !" << endl;
            // 直接跳过
            continue;
        }

        // 设置 read 非阻塞读取. 注意套接字仍然是阻塞的,这是为了阻塞 accept 函数
        if(!setSocketNoBlock(client_fd))
        {
            LOG(ERROR) << "Can not set socket " << client_fd << " No Block ! " 
                       << strerror(errno) << endl;
            continue;
        }
        
        // 禁用延迟读写
        // if(!setSocketNoDelay(client_fd))
        // {
        //     LOG(ERROR) << "Can not set socket " << client_fd << " No Delay ! "
        //                << strerror(errno) << endl;
        //     continue;
        // }

        // 将其放入线程池中并行执行
        int* curr_fd_ptr = new int(client_fd);
        thread_pool.appendTask(handlerConnect, curr_fd_ptr);
    }

    return 0;
}