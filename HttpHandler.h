#ifndef HTTPHANDLER_H
#define HTTPHANDLER_H

class HttpHandler
{
private:
    int client_fd_;
public:
    /**
     * @brief   显式指定 client fd
     * @param   fd 连接的 fd, 初始值为 -1
     */
    explicit HttpHandler(int fd = -1);

    /**
     * @brief   释放所有 HttpHandler 所使用的资源
     * @note    注意,不会主动关闭 client_fd
     */
    ~HttpHandler();

    /**
     * @brief   为当前连接启动事件循环
     * @note    在执行事件循环开始之前,一定要设置 client fd
     */ 
    void RunEventLoop();

    void setClientFd(int fd)    { client_fd_ = fd; }
    int getClientFd()          { return client_fd_; }
};

#endif