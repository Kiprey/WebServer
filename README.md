# WebServer-1.1(beta)

## 一、概述

WebServer-1.0 简单实现了一个基础的 **多并发网络服务程序** 。在该版本中，主要实现了以下重要内容：

- 线程互斥锁 & 条件变量的封装

- 线程池的设计，以支持并发

- 基础网络连接的实现

- http 协议的简略支持

  - 支持部分常用 HTTP 报文
    - 200 OK
    - 400 Bad Request
    - 500 Internal Server Error
    - 501 Not Implemented
    - 505 HTTP Version Not Supported
  - 支持 HTTP GET 请求
  - 支持 HTTP/1.1 **持续连接** 特性

WebServer-1.0运行时截图：

![image-20210512133857068](https://kiprey.github.io/2021/05/WebServer-1/image-20210512133857068.png)

> 1.0版本的项目代码位于 [Kiprey/WebServer CommitID: 6473f5d - github](https://github.com/Kiprey/WebServer/tree/6473f5d512097f235ab209b13b53e28d7946a0f6)
>
> 可以使用`git checkout v1.0`命令来切换版本。

WebServer-1.1 在原先 1.0 版本的基础上大量重构了代码，相对于旧版本来说，新版本主要更新了以下内容：

- 替换并发方式，从**多线程并发** 更换为 **epoll 并发**
- HTTP报文处理添加 POST 和 HEAD 方式的处理
- 支持自定义 WebServer 的 www 目录路径
- 使用 timerfd API，对每个 HTTP/1.1 Keep-Alive 的 TCP 链接设置了超时时间，超时后若还没有请求，则强制关闭该连接。
- 支持 Post 请求使用 CGI 程序。其中CGI程序可以是 shell 脚本、python脚本、ELF可执行文件等等。
- 支持自定义 www 目录路径，不再限制为当前工作目录。
- 支持更多的 Http 错误报文
  - 404 Not Found
  - 411 Length Required

WebServer-1.1 暂时没有运行时截图，不过也和 WebServer-1.0 差不多。 XD

> 1.1 beta版本的项目代码位于 [Kiprey/WebServer CommitID: 1fcf6c3e - github](https://github.com/Kiprey/WebServer/tree/1fcf6c3ec962ff3fb3cdc8726932bc932e088c63)
>
> 注意：该程序的实现大量参考了 [linyacool/WebServer - github](https://github.com/linyacool/WebServer) 的代码。

## 二、编译、运行与调试

- 使用以下指令编译:

  ```bash
  make
  ```

- WebServer-1.0使用以下指令运行

  ```bash
  ./WebServer <port>
  ```

  > 注意一些**特殊端口**的绑定需要使用 root 权限，例如 80 端口。

  WebServer-1.1 使用以下指令执行

  ```bash
  ./WebServer <port> [<www_dir>]
  ```

- 使用 GDB 进行调试。

## 三、技术文档

请点击 [此处 WebServer-1](docs/WebServer-1.md) 跳转至更加详细的WebServer1.0技术文档。

WebServer1.1技术文档因为时间原因暂时没有完成，但主要的技术细节已经以大量注释的形式写入了源代码中，可以直接阅读源代码来理解。

## 四、待完成任务

当前的WebServer-1.1为 beta 测试版本，并非正式版本。因为时间的限制，仍然有一部分工作没有完成、bug没有解决。待这部分工作完成后，即为正式发布的 WebServer-1.1 版本。

### 1. FIXME

- 大规模访问静态文件时,可能造成部分TCP连接处于close_wait状态，导致无法socket无法被关闭，占用大量文件描述符，但 epoll 没有检测到这类连接半关闭事件。
- 大规模访问CGI程序时，可能导致父进程无法杀死子进程，无法关闭管道，使得进程间通信管道占满整个进程空间。
- 大规模状态下，Timer超时处理程序可能存在问题，无法很好的完成工作。

### 2. TODO

- HttpHandler类中，尽管writen是阻塞写入,但需要注意的是,阻塞写入可能会极大影响Server性能,需要重新编写相关逻辑，切换为使用 epoll 来进行非阻塞写入。
- WebServer-1.1技术文档的编写。
