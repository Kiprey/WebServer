#include <algorithm>
#include <cassert>
#include <cstring>
#include <cctype>
#include <fcntl.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <memory>
#include <unistd.h>

#include "HttpHandler.h"
#include "ThreadPool.h"
#include "Log.h"
#include "Utils.h"

// 声明一下该静态成员变量
 // 如果先前没有设置 www 路径,则设置路径为 ./html
string HttpHandler::www_path = "html";

void Router::registerRoute(const std::string& path, RouteHandler handler) {
    routeTable_[path] = handler;
}

HTTP_ERROR_TYPE Router::route(const std::string& path, HttpHandler* handler) {
    auto it = routeTable_.find(path);
    if (it != routeTable_.end()) {
        return it->second(handler); // 调用对应的处理函数
    }
    return ERR_NOT_FOUND; // 如果没有找到匹配的路由，直接返回 404
}

HttpHandler::HttpHandler(std::shared_ptr<Epoll> epoll, int client_fd, std::unique_ptr<Timer> timer, std::shared_ptr<Router> router) 
      // 初始化 client 的 fd 和 epoll event
    : client_fd_(client_fd), timer_(std::move(timer)), epoll_(epoll), router_(router), curr_parse_pos_(0)
{
    // HTTP1.1下,默认是持续连接
    // 除非 client http headers 中带有 Connection: close
    isKeepAlive_ = true;
    // 初始化一些变量
    reset();
}

HttpHandler::~HttpHandler() {
    // 关闭客户套接字
    DEBUG_INFO("------------------------ "
         "Connection Closed (socket: %d)"
         "------------------------",
         client_fd_);
    timer_.reset();
    close(client_fd_);
}

void HttpHandler::reset()
{
    // 清除已经处理过的数据
    assert(request_.length() >= curr_parse_pos_);

    request_.clear();
    curr_parse_pos_ = 0;
    // 重设状态
    state_ = STATE_PARSE_URI;
    // 重置重试次数
    againTimes_ = maxAgainTimes;
    // 重置 headers_
    headers_.clear();
    // 重置 body
    http_body_.clear();
    // 重置超时时间
    timer_->setTime(timeoutPerRequest, 0);
}

HTTP_ERROR_TYPE HttpHandler::readRequest()
{
    DEBUG_INFO("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
         "- Request Packet -"
         ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> ");

    char buffer[MAXBUF];
    
    while(true)
    {
        // 非阻塞,使用 recv 读取
        ssize_t len = recv(client_fd_, buffer, MAXBUF, MSG_DONTWAIT);
        if(len < 0) {
            // 读取时没有出错
            if(errno == EAGAIN)
                return ERR_SUCCESS;
            else if(errno == EINTR)
                continue;
            return ERR_READ_REQUEST_FAIL;
        }
        else if(len == 0)
        {
            // 如果读取到的字节数为0,则说明 EOF, 远程连接已经被关闭
            return ERR_CONNECTION_CLOSED;
        }

        // 将读取到的数据组装起来
        string request(buffer, buffer + len);
        DEBUG_INFO("{%s}", escapeStr(request, MAXBUF).c_str());

        request_ += request;
    }
    return ERR_SUCCESS;
}

HTTP_ERROR_TYPE HttpHandler::parseURI()
{
    size_t pos1, pos2;
    
    pos1 = request_.find("\r\n");
    if(pos1 == string::npos)    return ERR_AGAIN;
    string&& first_line = request_.substr(0, pos1);
    // a. 查找get
    pos1 = first_line.find(' ');
    if(pos1 == string::npos)    return ERR_BAD_REQUEST;
    string methodStr = first_line.substr(0, pos1);

    string output_method = "Method: ";
    if(methodStr == "GET")
        method_ = METHOD_GET;
    else if(methodStr == "POST")
        method_ = METHOD_POST;
    else if(methodStr == "HEAD")
        method_ = METHOD_HEAD;
    else
        return ERR_NOT_IMPLEMENTED;
    DEBUG_INFO("Method: %s", methodStr.c_str());

    // b. 查找目标路径
    pos1++;
    pos2 = first_line.find(' ', pos1);
    if(pos2 == string::npos)    return ERR_BAD_REQUEST;

    // 获取path时,注意加上 www path
    path_ = first_line.substr(pos1, pos2 - pos1);
    
    DEBUG_INFO("Path: %s", path_.c_str());

    // c. 查看HTTP版本
    pos2++;
    string http_version_str = first_line.substr(pos2, first_line.length() - pos2);
    DEBUG_INFO("HTTP Version: %s", http_version_str.c_str());

    // 检测是否支持客户端 http 版本
    if(http_version_str == "HTTP/1.0")
        http_version_ = HTTP_1_0;
    else if (http_version_str == "HTTP/1.1")
        http_version_ = HTTP_1_1;
    else
        return ERR_HTTP_VERSION_NOT_SUPPORTED;

    // 更新curr_parse_pos_
    curr_parse_pos_ += first_line.length() + 2;
    return ERR_SUCCESS;
}

HTTP_ERROR_TYPE HttpHandler::parseHttpHeader()
{
    DEBUG_INFO("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
         "- Request Info -"
         ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");

    size_t pos1, pos2;
    for(pos1 = curr_parse_pos_;
        (pos2 = request_.find("\r\n", pos1)) != string::npos;
        pos1 = pos2 + 2)
    {
        string&& header = request_.substr(pos1, pos2 - pos1);
        // 如果遍历到了空头,则表示http header部分结束
        if(header.size() == 0)
        {
            curr_parse_pos_ = pos1 + 2;
            return ERR_SUCCESS;
        }
        pos1 = header.find(' ');

        if(pos1 == string::npos)    return ERR_BAD_REQUEST;

        // key 的格式： `XXX:`
        string&& key = header.substr(0, pos1);

        // 消除key里的最后一个冒号字符
        if(key.size() < 2 || key.back() != ':') return ERR_BAD_REQUEST;
        key.pop_back();

        // key 转小写
        transform(key.begin(), key.end(), key.begin(), ::tolower);
        // 获取 value
        string&& value = header.substr(pos1 + 1);

        DEBUG_INFO("HTTP Header: [%s : %s]", key.c_str(), value.c_str());

        headers_[key] = value;
    }

    // 执行到这里说明: 没有遍历到空头,即还有数据没有读完
    return ERR_AGAIN;
}

HTTP_ERROR_TYPE HttpHandler::parseBody()
{
    assert(method_ == METHOD_POST);
    
    auto content_len_iter = headers_.find("content-length");
    if(content_len_iter == headers_.end())
        return ERR_LENGTH_REQUIRED;

    string len_str = content_len_iter->second;
    if(!isNumericStr(len_str))
        return ERR_BAD_REQUEST;

    int len = atoi(len_str.c_str());

    if(request_.length() < curr_parse_pos_ + len)
        return ERR_AGAIN;
    http_body_ = request_.substr(curr_parse_pos_, len);

    // 输出剩余的 HTTP body
    DEBUG_INFO("HTTP Body: {%s}", escapeStr(http_body_, MAXBUF).c_str());

    return ERR_SUCCESS;    
}

HTTP_ERROR_TYPE HttpHandler::handleRequest()
{
    // 设置只在 HTTP/1.1时 默认允许 持续连接
    if(http_version_ == HTTP_1_0)
        isKeepAlive_ = false;

    // 获取header完成后,处理一下 Connection 头
    auto conHeaderIter = headers_.find("connection");
    if(conHeaderIter != headers_.end())
    {
        string value = conHeaderIter->second;
        transform(value.begin(), value.end(), value.begin(), ::tolower);
        if(value == "keep-alive")
            isKeepAlive_ = true;
    }

    // 开始处理请求
    // 对于普通的 GET / HEAD 请求,读取文件并发送
    if(method_ == METHOD_GET || method_ == METHOD_HEAD)
    {
        // 获取目标文件的信息
        string real_path = www_path + "/" + path_;
        // 检测目录穿越
        if(!is_path_parent(www_path, real_path))
            return ERR_NOT_FOUND;

        struct stat st;
        if(stat(real_path.c_str(), &st) == -1)
        {
            WARN("Can not get file [%s] state ! (%s)", real_path.c_str(), strerror(errno));
            if(errno == ENOENT)
                return ERR_NOT_FOUND;
            else
                return ERR_INTERNAL_SERVER_ERR;
        }
        // 如果试图打开一个文件夹,则添加 index.html
        if (S_ISDIR(st.st_mode)) {
            real_path += "/index.html";
            if(stat(real_path.c_str(), &st) == -1)
            {
                WARN("Can not get file [%s] state ! (%s)", real_path.c_str(), strerror(errno));
                if(errno == ENOENT)
                    return ERR_NOT_FOUND;
                else
                    return ERR_INTERNAL_SERVER_ERR;
            }
        }
        // 试图打开一个文件
        int file_fd;
        if((file_fd = open(real_path.c_str(), O_RDONLY, 0)) == -1)
        {
            WARN("File [%s] open failed ! (%s)", real_path.c_str(), strerror(errno));
            if(errno == ENOENT)
                // 如果打开失败,则返回404
                return ERR_NOT_FOUND;
            else
                // 如果是因为其他问题出错，则返回500
                return ERR_INTERNAL_SERVER_ERR;
        }  
        // 读取文件, 使用 mmap 来高速读取文件
        void* addr = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
        // 记得关闭文件描述符
        close(file_fd); 
        // 异常处理
        if(addr == MAP_FAILED)
        {
            WARN("Can not map file [%s] -> mem! (%s)", real_path.c_str(), strerror(errno));
            return ERR_INTERNAL_SERVER_ERR;
        }
        // 将数据从内存页存入至 responseBody
        char* file_data_ptr = static_cast<char*>(addr);
        string responseBody(file_data_ptr, file_data_ptr + st.st_size);
        // 记得删除内存
        int res = munmap(addr, st.st_size);
        if(res == -1)
            WARN("Can not unmap file [%s] -> mem! (%s)", real_path.c_str(), strerror(errno));
        // 获取 Content-type
        string suffix = real_path;
        // 通过循环找到最后一个 dot
        size_t dot_pos;
        while((dot_pos = suffix.find('.')) != string::npos)
            suffix = suffix.substr(dot_pos + 1);

        // 发送数据, 在该函数内部, METHOD_HEAD 不发送 http body
        return sendResponse("200", "OK", MimeType::getMineType(suffix), responseBody);
    }
    // 而对于POST来说, 需要解析输入内容
    else if(method_ == METHOD_POST)
        return router_->route(path_, this);
    else
        return ERR_INTERNAL_SERVER_ERR;
    UNREACHABLE();
    return ERR_SUCCESS;
}

bool HttpHandler::handleErrorType(HTTP_ERROR_TYPE err)
{
    // 除了 ERR_SUCESS 和 ERR_AGAIN 没有设置 state 以外, 其他 case 都设置了 state_
    bool isSuccess = false;
    switch(err)
    {
    case ERR_SUCCESS:
        isSuccess = true;
        /* 注意这里没有设置 STATE */
        break;
    case ERR_READ_REQUEST_FAIL:
        ERROR("HTTP Read request failed ! (%s)", strerror(errno));
        state_ = STATE_FATAL_ERROR;
        break;
    case ERR_AGAIN:
        --againTimes_;
        DEBUG_INFO("HTTP waiting for more messages...");
        /* 注意这里没有设置 STATE , 与 ERR_SUCESS一样 */
        if(againTimes_ <= 0)
        {
            state_ = STATE_FATAL_ERROR;
            WARN("Reach max read times");
        }
        break;
    case ERR_CONNECTION_CLOSED:
        DEBUG_INFO("HTTP Socket(%d) was closed.", client_fd_);
        state_ = STATE_FATAL_ERROR;
        break;
    case ERR_SEND_RESPONSE_FAIL:
        ERROR("Send Response failed !");
        state_ = STATE_FATAL_ERROR;
        break;
    case ERR_BAD_REQUEST:
        WARN("HTTP Bad Request.");
        sendErrorResponse("400", "Bad Request");
        state_ = STATE_ERROR;
        break;
    case ERR_NOT_FOUND:
        WARN("HTTP Not Found.");
        sendErrorResponse("404", "Not Found");
        state_ = STATE_ERROR;
        break;
    case ERR_LENGTH_REQUIRED:
        WARN("HTTP Length Required.");
        sendErrorResponse("411", "Length Required");
        state_ = STATE_ERROR;
        break;
    case ERR_NOT_IMPLEMENTED:
        WARN("HTTP Request method is not implemented.");
        sendErrorResponse("501", "Not Implemented");
        state_ = STATE_ERROR;
        break;
    case ERR_INTERNAL_SERVER_ERR:
        WARN("HTTP Internal Server Error.");
        sendErrorResponse("500", "Internal Server Error");
        state_ = STATE_ERROR;
        break;
    case ERR_HTTP_VERSION_NOT_SUPPORTED:
        WARN("HTTP Request HTTP Version Not Supported.");
        sendErrorResponse("505", "HTTP Version Not Supported");
        state_ = STATE_ERROR;
        break;
    default:
        UNREACHABLE();
    }
    return isSuccess;
}

HTTP_ERROR_TYPE HttpHandler::sendResponse(const string& responseCode, const string& responseMsg, 
                            const string& responseBodyType, const string& responseBody)
{
    std::stringstream sstream;
    sstream << "HTTP/1.1" << " " << responseCode << " " << responseMsg << "\r\n";
    sstream << "Connection: " << (isKeepAlive_ ? "Keep-Alive" : "Close") << "\r\n";
    if(isKeepAlive_)
        // Keep-Alive 头中, timeout 表示超时时间(单位s), max表示最多接收请求次数,超过则断开.
        sstream << "Keep-Alive: timeout=" << timeoutPerRequest << ", max=" << againTimes_ << "\r\n";
    sstream << "Server: WebServer" << "\r\n";
    sstream << "Content-length: " << responseBody.size() << "\r\n";
    sstream << "Content-type: " << responseBodyType << "\r\n";
    // 设置 Content-Encoding: identity 表示不进行编码，始终返回原始内容
    sstream << "Content-Encoding: identity\r\n";
    sstream << "\r\n";
    // 如果是 HEAD 请求,则不发送 http body
    if(method_ != METHOD_HEAD)
        sstream << responseBody;

    string&& response = sstream.str();

    ssize_t len = writen(client_fd_, (void*)response.c_str(), response.size());

    // 输出返回的数据
    DEBUG_INFO("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<- Response Packet ->>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> ");
    DEBUG_INFO("{%s}", escapeStr(response, MAXBUF).c_str());

    if(len < 0 || static_cast<size_t>(len) != response.size())
        return ERR_SEND_RESPONSE_FAIL;
    return ERR_SUCCESS;
}

HTTP_ERROR_TYPE HttpHandler::sendErrorResponse(const string& errCode, const string& errMsg)
{
    string errStr = errCode + " " + errMsg;
    string responseBody = 
                "<html>"
                "<title>" + errStr + "</title>"
                "<body>" + errStr + 
                    "<hr><em> Web Server</em>"
                "</body>"
                "</html>";
    return sendResponse(errCode, errMsg, "text/html", responseBody);
}

bool HttpHandler::RunEventLoopAndReEpoll()
{
    // 从socket读取请求数据, 如果读取失败,或者断开连接
    if(!handleErrorType(readRequest()))
        // 直接断开连接
        return false;
    
    // 解析信息 ------------------------------------------
    // 1. 先解析第一行
    if(state_ == STATE_PARSE_URI && handleErrorType(parseURI()))
        state_ = STATE_PARSE_HEADER;
    // 2. 解析每一条http header
    if(state_ == STATE_PARSE_HEADER && handleErrorType(parseHttpHeader()))
        state_ = STATE_PARSE_BODY;
    // 3. 对于 post 解析 http body
    if(state_ == STATE_PARSE_BODY)
    {
        if(method_ != METHOD_POST || handleErrorType(parseBody()))
            state_ = STATE_ANALYSI_REQUEST;
    }
    // 4. 开始处理数据
    if(state_ == STATE_ANALYSI_REQUEST && handleErrorType(handleRequest()))
        state_ = STATE_FINISHED;

    // 开始处理当前状态
    // 如果这个过程中有任何非致命错误, 或者当前过程圆满结束
    if(state_ == STATE_ERROR || state_ == STATE_FINISHED)
    {
        // 如果 keep Alive, 则重置状态, 并跳出 if 到最后的return 处重新放入 epoll 中
        if(isKeepAlive_)
            reset();
        else 
            // 否则,既然已经发生了错误 / 完成了请求,则直接销毁当前实例
            return false;
    }
    // 如果是致命错误,则直接返回 false
    else if(state_ == STATE_FATAL_ERROR)
        return false;

    // 执行到这里则表示需要更多数据,因此重新放入 epoll 中
    return true;
}