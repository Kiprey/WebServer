#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "Utils.h"

std::ostream& logmsg(int flag)
{
    if(flag == ERROR)
    {
        std::cerr << "[ERROR]\t";
        return std::cerr;       
    }
    else if(flag == INFO)
    {
        std::cout << "[INFO]\t";
        return std::cout;
    }
    else
    {
        logmsg(ERROR) << "错误的 LOG 选择" << std::endl;
        abort();
    }
}

int socket_bind_and_listen(int port)
{
    int listen_fd = 0;
    // 开始创建 socket, 注意这是阻塞模式的socket
    // AF_INET      : IPv4 Internet protocols  
    // SOCK_STREAM  : TCP socket
    if((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        return -1;

    // 绑定端口
    sockaddr_in server_addr;
    // 初始化一下
    memset(&server_addr, '\0', sizeof(server_addr));
    // 设置一下基本操作
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htonl((unsigned short)port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 试着bind
    if(bind(listen_fd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1)
        return -1;
    // 试着listen, 设置最大队列长度为 1024
    if(listen(listen_fd, 1024) == -1)
        return -1;

    return listen_fd;
}