#include <arpa/inet.h>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "Log.h"
#include "MutexLock.h"
#include "Utils.h"

int socket_bind_and_listen(int port)
{
    int listen_fd = 0;
    // 开始创建 socket, 注意这是阻塞模式的socket
    // AF_INET      : IPv4 Internet protocols  
    // SOCK_STREAM  : TCP socket
    if((listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) == -1)
        return -1;

    // 绑定端口
    sockaddr_in server_addr;
    // 初始化一下
    memset(&server_addr, '\0', sizeof(server_addr));
    // 设置一下基本操作
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    // 端口复用
    int opt = 1;
    if(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
        return -1;
    // 试着bind
    if(bind(listen_fd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1)
        return -1;
    // 试着listen, 设置最大队列长度为 1024
    if(listen(listen_fd, 1024) == -1)
        return -1;

    return listen_fd;
}

bool setFdNoBlock(int fd)
{
    // 获取fd对应的flag
    int flag = fcntl(fd, F_GETFD);
    if(flag == -1)
        return -1;
    flag |= O_NONBLOCK;
    if(fcntl(fd, F_SETFL, flag) == -1)
        return false;
    return true;
}

bool setSocketNoDelay(int fd)
{
    int enable = 1;
    if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&enable, sizeof(enable)) == -1)
        return false;
    return true;
}

ssize_t readn(int fd, void* buf, size_t len)
{
    // 这里将 void* 转换成 char* 是为了在下面进行自增操作
    char *pos = (char*)buf;
    size_t leftNum = len;
    ssize_t readNum = 0;
    while(leftNum > 0)
    {
        // 尝试循环读取,如果报错,则进行判断
        // 注意, read 的返回值为0则表示读取到 EOF,是正常现象
        ssize_t tmpRead = read(fd, pos, leftNum);
        
        if(tmpRead < 0)
        {
            if(errno == EINTR)
                tmpRead = 0;
            // 如果始终读取不到数据,则提前返回,因为这个取决于远程 fd,无法预测要等多久
            else if (errno == EAGAIN)
                return readNum;
            else
                return -1;
        }
        // 读取的0,则说明远程连接已被关闭
        if(tmpRead == 0)
            break;
        readNum += tmpRead;
        pos += tmpRead;

        leftNum -= tmpRead;
    }
    return readNum;
}

ssize_t writen(int fd, const void* buf, size_t len, bool isWrite)
{
    // 这里将 void* 转换成 char* 是为了在下面进行自增操作
    char *pos = (char*)buf;
    size_t leftNum = len;
    ssize_t writtenNum = 0;
    while(leftNum > 0)
    {
        ssize_t tmpWrite = 0;

        if(isWrite)
            tmpWrite = write(fd, pos, leftNum);
        else
            tmpWrite = send(fd, pos, leftNum, 0);

        // 尝试循环写入,如果报错,则进行判断
        // 注意,write返回0属于异常现象,因此判断时需要包含
        if(tmpWrite < 0)
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

void handleSigpipe()
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if(sigaction(SIGPIPE, &sa, NULL) == -1)
        ERROR("Ignore SIGPIPE failed! (%s)", strerror(errno));
}

void printConnectionStatus(int client_fd_, string prefix)
{
    // 输出连接信息 [Server]IP:PORT <---> [Client]IP:PORT
    sockaddr_in serverAddr, peerAddr;
    socklen_t serverAddrLen = sizeof(serverAddr);
    socklen_t peerAddrLen = sizeof(peerAddr);

    if((getsockname(client_fd_, (struct sockaddr *)&serverAddr, &serverAddrLen) != -1)
        && (getpeername(client_fd_, (struct sockaddr *)&peerAddr, &peerAddrLen) != -1))
        INFO("%s: (socket %d) [Server] %s:%d <---> [Client] %s:%d",
            prefix.c_str(), client_fd_,
            inet_ntoa(serverAddr.sin_addr), ntohs(serverAddr.sin_port),
            inet_ntoa(peerAddr.sin_addr), ntohs(peerAddr.sin_port));
    else
        ERROR("printConnectionStatus failed ! (%s)", strerror(errno));
}

string escapeStr(const string& str, size_t MAXBUF)
{
    string msg = str;
    // 遍历所有字符
    for(size_t i = 0; i < msg.length(); i++)
    {
        char ch = msg[i];
        // 如果当前字符无法打印,则转义
        if(!isprint(ch))
        {
            // 这里只对\r\n做特殊处理
            string substr;
            if(ch == '\r')
                substr = "\\r";
            else if(ch == '\n')
                substr = "\\n";
            else
            {
                char hex[10];
                // 注意这里要设置成 unsigned,即零扩展
                snprintf(hex, 10, "\\x%02x", static_cast<unsigned char>(ch));
                substr = hex;
            }
            msg.replace(i, 1, substr);
        }
    }
    // 将读取到的数据输出
    if(msg.length() > MAXBUF)
        return msg.substr(0, MAXBUF) + " ... ... ";
    else
        return msg;
}

bool isNumericStr(string str)
{
    for(size_t i = 0; i < str.length(); i++)
        if(!isdigit(str[i]))
            return false;
    return true;
}

size_t closeRemainingConnect(int listen_fd, int* idle_fd) {
    close(*idle_fd);

    size_t count = 0;
    for(;;) {
        int client_fd = accept4(listen_fd, nullptr, nullptr, 
                SOCK_NONBLOCK | SOCK_CLOEXEC);
        if(client_fd == -1 && errno == EAGAIN)
            break;
        close(client_fd);
        ++count;
    }
    // 重新恢复空闲描述符
    *idle_fd = open("/dev/null", O_RDONLY | O_CLOEXEC); 
    return count;
}
