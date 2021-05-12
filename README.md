# WebServer 1.0

## 一、概述

WebServer 1.0 简单实现了一个基础的 **多并发网络服务程序** 。在该版本中，主要实现了以下重要内容：

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

运行示例：

![image-20210512133857068](https://kiprey.github.io/2021/05/WebServer-1/image-20210512133857068.png)

> 注意：该程序的实现大量参考了 [linyacool/WebServer - github](https://github.com/linyacool/WebServer) 的代码。

## 二、编译、运行与调试

- 使用以下指令编译:

  ```bash
  make
  ```

- 使用以下指令运行

  ```bash
  ./WebServer <port>
  ```

  > 注意一些**特殊端口**的绑定需要使用 root 权限，例如 80 端口。

- 使用 GDB 进行调试。

## 三、技术文档

请点击 [此处 WebServer-1](docs/WebServer-1.md) 跳转至更加详细的技术文档。
