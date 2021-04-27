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

ssize_t readn(int fd, void*buf, size_t len)
{
    // 这里将 void* 转换成 char* 是为了在下面进行自增操作
    char *pos = (char*)buf;
    size_t leftNum = len;
    ssize_t readNum = 0;
    while(leftNum > 0)
    {
        ssize_t tmpRead = 0;
        // 尝试循环读取,如果报错,则进行判断
        // 注意, read 的返回值为0则表示读取到 EOF,是正常现象
        if((tmpRead = read(fd, pos, leftNum)) < 0)
        {
            if(errno == EINTR)
                tmpRead = 0;
            // 如果始终读取不到数据,则提前返回,因为这个取决于远程 fd,无法预测要等多久
            else if (errno == EAGAIN)
                return readNum;
            else
                return -1;
        }
        if(tmpRead == 0)
            break;
        readNum += tmpRead;
        pos += tmpRead;
        leftNum -= tmpRead;
    }
    return readNum;
}

ssize_t writen(int fd, void*buf, size_t len)
{
    // 这里将 void* 转换成 char* 是为了在下面进行自增操作
    char *pos = (char*)buf;
    size_t leftNum = len;
    ssize_t writtenNum = 0;
    while(leftNum > 0)
    {
        ssize_t tmpWrite = 0;
        // 尝试循环写入,如果报错,则进行判断
        // 注意,write返回0属于异常现象,因此判断时需要包含
        if((tmpWrite = write(fd, pos, leftNum)) <= 0)
        {
            // 与read不同的是,如果 EAGAIN,则继续重复写入,因为写入操作是有Server这边决定的
            if(errno == EINTR || errno == EAGAIN)
                tmpWrite = 0;
            else
                return -1;
        }
        if(tmpWrite == 0)
            break;
        writtenNum += tmpWrite;
        pos += tmpWrite;
        leftNum -= tmpWrite;
    }
    return writtenNum;
}