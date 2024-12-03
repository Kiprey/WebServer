#ifndef HTTPHANDLER_H
#define HTTPHANDLER_H

#include <iostream>
#include <unordered_map>
#include <functional>

#include "Epoll.h"
#include "Timer.h"

// HttpHandler内部错误 
enum HTTP_ERROR_TYPE {
    ERR_SUCCESS = 0,                // 无错误

    ERR_READ_REQUEST_FAIL,          // 读取请求数据失败
    ERR_AGAIN,                      // 读取的数据不够,需要等待下一次读取到的数据再来解析
    ERR_CONNECTION_CLOSED,          // 远程连接已关闭

    ERR_SEND_RESPONSE_FAIL,         // 响应包发送失败

    ERR_BAD_REQUEST,                // 用户的请求包中存在错误,无法解析                   400 Bad Request
    ERR_NOT_FOUND,                  // 目标文件不存在                                 404 Not Found
    ERR_LENGTH_REQUIRED,            // POST请求中没有 Content-Length 请求头            411 Length Required

    ERR_NOT_IMPLEMENTED,            // 不支持一些特定的请求操作                         501 Not Implemented
    ERR_INTERNAL_SERVER_ERR,        // 程序内部错误                                   500 Internal Server Error
    ERR_HTTP_VERSION_NOT_SUPPORTED  // 不支持当前客户端的http版本                       505 HTTP Version Not Supported
};

class HttpHandler;
// 定义处理函数类型
using RouteHandler = std::function<HTTP_ERROR_TYPE(HttpHandler*)>;

class Router {
public:
    // 注册路由的方法
    void registerRoute(const std::string& path, RouteHandler handler);
    // 查找并执行路由
    HTTP_ERROR_TYPE route(const std::string& path, HttpHandler* handler);

private:
    std::unordered_map<std::string, RouteHandler> routeTable_;
};

/**
 * @brief HttpHandler 类处理每一个客户端连接,并根据读入的http报文,动态返回对应的response
 *        其支持的 HTTP 版本为 HTTP/1.1
 */ 
class HttpHandler
{
public:
    
    /**
     * @brief   显式指定 client fd
     * @param   epoll    epoll 实例
     * @param   client_fd   连接的 client_fd
     * @param   timer       给当前连接限制时间的timer
     * @param   router     路由表
     */
    explicit HttpHandler(std::shared_ptr<Epoll> epoll, int client_fd, std::unique_ptr<Timer>&& timer, std::shared_ptr<Router> router);

    /**
     * @brief   释放所有 HttpHandler 所使用的资源
     * @note    注意,不会主动关闭 client_fd
     */
    ~HttpHandler();
    void destroy();

    /**
     * @brief   为当前连接启动事件循环
     * @return  若当前文件描述符的数据没有处理完成, 则返回 true;
     *          如果完成了所有的事件,需要被释放时则返回 false
     * @note    在执行事件循环开始之前,一定要设置 client fd
     */ 
    bool RunEventLoopAndReEpoll();

    // 只有getFd,没有setFd,因为Fd必须在创造该实例时被设置
    int getClientFd()           { return client_fd_; }
    int getTimerFd()           { 
        assert(timer_); 
        return timer_->getFd(); 
    }
    // 获取 client_fd 和 timer_fd 所需要设置的 epoll 触发条件
    constexpr int getClientTriggerCond() { return EPOLLET | EPOLLIN | EPOLLONESHOT | EPOLLRDHUP | EPOLLHUP; }
    constexpr int getTimerTriggerCond()  { return EPOLLET | EPOLLIN | EPOLLONESHOT; }
    // 获取 client 和 timer 的 epoll event
    void setClientEpollEventCallback(std::unique_ptr<EpollEventCallback>&& cb) { 
        assert(!client_event_);
        client_event_ = std::move(cb); 
    }
    void setTimerEpollEventCallback(std::unique_ptr<EpollEventCallback>&& cb) { 
        assert(!timer_event_);
        timer_event_ = std::move(cb); 
    }
    std::unique_ptr<EpollEventCallback>* const getClientEpollEventCallback() { 
        assert(client_event_);
        return &client_event_; 
    }
    std::unique_ptr<EpollEventCallback>* const getTimerEpollEventCallback()  { 
        assert(timer_event_);
        return &timer_event_; 
    }

    // 设置HTTP处理时, www文件夹的路径
    static void setWWWPath(string path) { www_path = path; };
    static string getWWWPath()          { return www_path; }

    // HttpHandler 内部状态
    enum STATE_TYPE {
        STATE_PARSE_URI,          // 解析 HTTP 报文中的第一行, [METHOD URI HTTP_VERSION]
        STATE_PARSE_HEADER,       // 解析 HTTP header
        STATE_PARSE_BODY,         // 解析 HTTP body (只针对 POST 请求解析. 注: GET 请求不会解析多余的body)
        STATE_ANALYSI_REQUEST,    // 解析获取到的整体报文,处理并发送对应的响应报文
        STATE_FINISHED,           // 当前报文已经解析完毕
        STATE_ERROR,              // 遇到了可恢复的错误
        STATE_FATAL_ERROR         // 遇到了无法恢复的错误,即将断开连接并销毁当前实例
    };
    // 获取状态
    STATE_TYPE getState()   { return state_; }

    // 获取一些已经解析好的内容
    HTTP_ERROR_TYPE getHttpBody(string& output) { 
        if (state_ >= STATE_PARSE_BODY) {
            output = http_body_; 
            return ERR_SUCCESS;
        }
        return ERR_BAD_REQUEST;
    }
    HTTP_ERROR_TYPE getHttpHeader(const string& key, string& output) { 
        if (state_ > STATE_PARSE_HEADER) {
            output = headers_[key]; 
            return ERR_SUCCESS;
        }
        return ERR_AGAIN;
    }

    /**
     * @brief   发送响应报文给客户端
     * @param   responseCode        http 状态码, http报文第二个字段
     * @param   responseMsg         http 报文第三个字段
     * @param   responseBodyType    返回的body类型,即 Content-type
     * @param   responseBody        返回的body内容
     * @return  ERR_SUCCESS 表示成功发送, 其他则表示发送过程存在错误
     */
    HTTP_ERROR_TYPE sendResponse(const string& responseCode, const string& responseMsg, 
                      const string& responseBodyType, const string& responseBody);
    
    /**
     * @brief 发送错误信息至客户端
     * @param errCode   错误http状态码
     * @param errMsg    错误信息, http报文第三个字段
     * @return ERR_SUCCESS 表示成功发送, 其他则表示发送过程存在错误
     */
    HTTP_ERROR_TYPE sendErrorResponse(const string& errCode, const string& errMsg);

private:
    // 请求的 HTTP 版本号
    enum HTTP_VERSION{
        HTTP_1_0,           // HTTP/1.0
        HTTP_1_1,           // HTTP/1.1
    };

    // 支持的请求方式
    enum METHOD_TYPE {
        METHOD_GET,         // GET 请求
        METHOD_POST,        // POST 请求
        METHOD_HEAD         // HEAD 请求,与 GET 处理方式相同,但不返回 body
    };

    // 当前 HTTP handler 的 www 工作目录, 默认情况下为当前工作目录
    static string www_path;

    // 一些常量
    const size_t MAXBUF = 1024;         // 缓冲区大小
    const int maxAgainTimes = 10;       // 最多重试次数
    const int timeoutPerRequest = 10;   // 单个请求的超时时间(s)

    // 相关描述符
    int client_fd_;
    // 注意！！！：该 Callback 会持有 epoll, handler(this), thread_pool 的引用
    std::unique_ptr<EpollEventCallback> client_event_;

    std::unique_ptr<Timer> timer_;
    // 注意！！！：该 Callback 会持有 handler(this) 的引用
    std::unique_ptr<EpollEventCallback> timer_event_;

    std::shared_ptr<Epoll> epoll_;

    // http 请求包的所有数据
    std::string request_;
    // http 头部
    std::unordered_map<string, string> headers_; 
    // 请求方式
    METHOD_TYPE method_;
    // 请求路径
    std::string path_;
    // http版本号
    HTTP_VERSION http_version_;
    // 当前handler 状态
    STATE_TYPE state_;
    // 重试次数
    int againTimes_;
    // http body 数据
    std::string http_body_;

    // 是否是 `持续连接`
    bool isKeepAlive_;

    // 路由表
    std::shared_ptr<Router> router_;

    /** 
     * @brief 当前解析读入数据的位置
     * @note 该成员变量只在 
     *      readRequest -> parseURI -> parseHttpHeader -> RunEventLoopAndReEpoll 
     * 内部中使用
     */
    size_t curr_parse_pos_;

    /**
     * @brief 初始化,清空所有数据
     */
    void reset();

    /**
     * @brief 从client_fd_中读取数据至 request_中
     * @return ERR_SUCCESS 表示读取成功;
     *         ERR_AGAIN 表示读取过程中缺失数据,需要等到下次再读
     *         其他则表示读取过程存在错误
     * @note 内部函数recvn在错误时会产生 errno
     */
    HTTP_ERROR_TYPE readRequest();

    /**
     * @brief 从0位置处解析 请求方式\URI\HTTP版本等
     * @return ERR_SUCCESS 表示读取成功;
     *         ERR_AGAIN 表示读取过程中缺失数据,需要等到下次再读
     *         其他则表示读取过程存在错误
     */
    HTTP_ERROR_TYPE parseURI();

    /**
     * @brief 从request_中的pos位置开始解析 http header
     * @return ERR_SUCCESS 表示读取成功;
     *         ERR_AGAIN 表示读取过程中缺失数据,需要等到下次再读
     *         其他则表示读取过程存在错误
     */
    HTTP_ERROR_TYPE parseHttpHeader();
    
    /**
     * @brief 解析 http body
     * @return ERR_SUCCESS 表示读取成功;
     *         ERR_AGAIN 表示读取过程中缺失数据,需要等到下次再读
     *         不存在其他错误情况
     */
    HTTP_ERROR_TYPE parseBody();

    /**
     * @brief 处理获取到的完整请求
     * @return ERR_SUCCESS 表示读取成功;
     *         其他则表示读取过程存在错误
     */
    HTTP_ERROR_TYPE handleRequest();

    /**
     * @brief 处理传入的错误类型
     * @param err 错误类型
     * @return 如果传入 ERR_SUCCESS 则返回 true,否则返回 false
     */
    bool handleErrorType(HTTP_ERROR_TYPE err);
};

class MimeType
{
private:
    // (suffix -> type)
    std::unordered_map<string, string> mime_map_;

    string getMineType_(string suffix)
    {
        if(mime_map_.find(suffix) != mime_map_.end())
            return mime_map_[suffix];
        else
            return mime_map_["default"];
    }
public:
    MimeType()
    {
        mime_map_["doc"] = "application/msword";
        mime_map_["gz"] = "application/x-gzip";
        mime_map_["ico"] = "application/x-ico";

        mime_map_["gif"] = "image/gif";
        mime_map_["jpg"] = "image/jpeg";
        mime_map_["png"] = "image/png";
        mime_map_["bmp"] = "image/bmp";

        mime_map_["mp3"] = "audio/mp3";
        mime_map_["avi"] = "video/x-msvideo";

        mime_map_["html"] = "text/html";
        mime_map_["htm"] = "text/html";
        mime_map_["css"] = "text/html";
        mime_map_["js"] = "text/html";

        mime_map_["c"] = "text/plain";
        mime_map_["txt"] = "text/plain";
        mime_map_["default"] = "text/plain";
    }

    static string getMineType(string suffix)
    {
        static MimeType _mimeTy;
        return _mimeTy.getMineType_(suffix);
    }
};

#endif