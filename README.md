# WebServer-1.1

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

![image-20210512133857068](docs/img/image-20210512133857068.png)

> 1.0版本的项目代码位于 [Kiprey/WebServer CommitID: 6473f5d - github](https://github.com/Kiprey/WebServer/tree/6473f5d512097f235ab209b13b53e28d7946a0f6)
>
> 可以使用`git checkout v1.0`命令来切换版本。

WebServer-1.1 在原先 1.0 版本的基础上大量重构了代码，相对于旧版本来说，新版本主要更新了以下内容：

- 替换并发方式，从**多线程并发** 更换为 **epoll 并发**
- HTTP报文处理添加 POST 和 HEAD 方式的处理
- 支持自定义 WebServer 的 www 目录路径
- 使用 timerfd API，对每个 HTTP/1.1 Keep-Alive 的 TCP 链接设置了超时时间，超时后若还没有请求，则强制关闭该连接。
- 支持 Post 请求使用 CGI 程序。其中CGI程序是可执行文件（例如shell脚本，附带 `#!` 的 python 脚本， ELF可执行文件等等）。
- 支持自定义 www 目录路径，不再限制为当前工作目录。
- 支持互斥 pretty LOG 输出
- 支持更多的 Http 错误报文
  - 404 Not Found
  - 411 Length Required
- 支持 Address Sanitizer 检测当前程序的潜在漏洞
- 更多的功能等待发现......

WebServer-1.1 运行时截图：

![image-20211026112349](docs/img/image-20211026112349.png)

> 可以使用`git checkout v1.1`命令来切换版本

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

## 四、测试方式

- 单个测试

  ```bash
  # GET 请求
  curl http://localhost:8012/html/index.html
  curl -d <http_body> http://localhost:8012/html/CGI/base64script
  # POST 请求

  ```

- 使用 apache 测试工具 `ab` 来进行大批量测试

  ```bash
  # -c 并发数
  # -n 总请求数
  # -s 单个请求的超时时间

  # GET 测试
  ab -c 500 -n 10000 -s 300 http://127.0.0.1:8012/html/index.html
  # POST 测试
  ab -c 500 -n 10000 -s 300 -p ignore_post.txt http://127.0.0.1:8012/html/CGI/base64script
  ```