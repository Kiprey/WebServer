#ifndef HTTPHANDLER_H
#define HTTPHANDLER_H

#include <iostream>
#include <map>
#include <unordered_map>

#include "Epoll.h"

using namespace std;

/**
 * @brief HttpHandler 类处理每一个客户端连接,并根据读入的http报文,动态返回对应的response
 *        其支持的 HTTP 版本为 HTTP/1.1
 * @note  该类只实现了部分异常处理,没有涵盖大部分的异常(不过暂时也够了)
 */ 
class HttpHandler
{
public:

    /**
     * @brief   显式指定 client fd
     * @param   epoll_fd    epoll 实例相关的描述符
     * @param   fd          连接的 fd, 初始值为 -1
     */
    explicit HttpHandler(Epoll* epoll, int fd);

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

    // 只有getFd,没有setFd,因为Fd必须在创造该实例时被设置
    int getClientFd()           { return client_fd_; }
    Epoll* getEpoll()           { return epoll_; }

    // 判断当前连接是否关闭
    bool isConnectClosed()      { return isClosed_; }
    // 关闭当前连接, 该 HttpHandler 实例将在 事件循环结束时被释放
    void setConnectClosed()     { isClosed_ = true; }

    // 设置HTTP处理时, www文件夹的路径
    static void setWWWPath(string path) { www_path = path; };
    static string getWWWPath()          { return www_path; }

private:
    /**
     *  @brief HttpHandler内部错误 
     */ 
    enum ERROR_TYPE {
        ERR_SUCCESS = 0,                // 无错误
        ERR_READ_REQUEST_FAIL,          // 读取请求数据失败
        ERR_NOT_IMPLEMENTED,            // 不支持一些特定的请求操作,例如 Post
        ERR_HTTP_VERSION_NOT_SUPPORTED, // 不支持当前客户端的http版本
        ERR_INTERNAL_SERVER_ERR,        // 程序内部错误
        ERR_CONNECTION_CLOSED,          // 远程连接已关闭
        ERR_BAD_REQUEST,                // 用户的请求包中存在错误,无法解析  
        ERR_SEND_RESPONSE_FAIL          // 响应包发送失败
    };

    // 当前 HTTP handler 的 www 工作目录
    static string www_path;
    const size_t MAXBUF = 1024;

    int client_fd_;
    Epoll* epoll_;
    // http 请求包的所有数据
    string request_;
    // http 头部
    unordered_map<string, string> headers_; 
    
    // 请求方式
    string method_;
    // 请求路径
    string path_;
    // http版本号
    string http_version_;
    // 是否是 `持续连接`
    // NOTE: 为了防止bug的产生,对于每一个类中的isKeepAlive_来说,
    //       值只能从 true -> false,而不能再次从 false -> true
    bool isKeepAlive_;

    // 当前解析读入数据的位置
    /** 
     * NOTE: 该成员变量只在 
     *      readRequest -> parseURI -> parseHttpHeader -> RunEventLoop 
     * 内部中使用
     */
    size_t pos_;

    // 当前连接是否关闭
    bool isClosed_;
    
    /**
     * @brief 从client_fd_中读取数据至 request_中
     * @return 0 表示读取成功, 其他则表示读取过程存在错误
     * @note 内部函数recvn在错误时会产生 errno
     */
    ERROR_TYPE readRequest();

    /**
     * @brief 从0位置处解析 请求方式\URI\HTTP版本等
     * @return 0 表示成功解析, 其他则表示解析过程存在错误
     */
    ERROR_TYPE parseURI();

    /**
     * @brief 从request_中的pos位置开始解析 http header
     * @return 0 表示成功解析, 其他则表示解析过程存在错误
     */
    ERROR_TYPE parseHttpHeader();
    
    /**
     * @brief   发送响应报文给客户端
     * @param   responseCode        http 状态码, http报文第二个字段
     * @param   responseMsg         http 报文第三个字段
     * @param   responseBodyType    返回的body类型,即 Content-type
     * @param   responseBody        返回的body内容
     * @return 0 表示成功发送, 其他则表示发送过程存在错误
     */
    ERROR_TYPE sendResponse(const string& responseCode, const string& responseMsg, 
                      const string& responseBodyType, const string& responseBody);
    
    /**
     * @brief 发送错误信息至客户端
     * @param errCode   错误http状态码
     * @param errMsg    错误信息, http报文第三个字段
     * @return 0 表示成功发送, 其他则表示发送过程存在错误
     */
    ERROR_TYPE handleError(const string& errCode, const string& errMsg);
};

class MimeType
{
private:
    // (suffix -> type)
    map<string, string> mime_map_;

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
        static MimeType* _mimeTy = new MimeType();
        return _mimeTy->getMineType_(suffix);
    }
};

#endif