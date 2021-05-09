#ifndef HTTPHANDLER_H
#define HTTPHANDLER_H

#include <iostream>
#include <map>
#include <unordered_map>
using namespace std;

class HttpHandler
{
private:
    const int MAXBUF = 1024;

    int client_fd_;
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

    /**
     * @brief 将当前client_fd_对应的连接信息,以 LOG(INFO) 的形式输出
     */
    void printConnectionStatus();

    /**
     * @brief 从client_fd_中读取数据至 request_中
     */
    void readRequest();

    /**
     * @brief 从0位置处解析 请求方式\URI\HTTP版本等
     * @return 解析终止的位置
     */
    size_t parseURI();

    /**
     * @brief 从request_中的pos位置开始解析 http header
     * @param pos 起始解析的位置
     * @return 解析终止的位置
     */
    size_t parseHttpHeader(size_t pos);
    
    /**
     * @brief   发送响应报文给客户端
     * @param   responseCode        http 状态码, http报文第二个字段
     * @param   responseMsg         http 报文第三个字段
     * @param   responseBodyType    返回的body类型,即 Content-type
     * @param   responseBody        返回的body内容
     * @return  true代表发送成功,false代表发送失败
     */
    bool sendResponse(const string& responseCode, const string& responseMsg, 
                      const string& responseBodyType, const string& responseBody);
    
    /**
     * @brief 发送错误信息至客户端
     * @param errCode   错误http状态码
     * @param errMsg    错误信息, http报文第三个字段
     * @return true 代表发送成功, false 代表发送失败
     */
    bool handleError(const string& errCode, const string& errMsg);

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
     * @note    1. 在执行事件循环开始之前,一定要设置 client fd
     *          2. 异常处理不完备
     */ 
    void RunEventLoop();

    // 只有getFd,没有setFd,因为Fd必须在创造该实例时被设置
    int getClientFd()           { return client_fd_; }
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