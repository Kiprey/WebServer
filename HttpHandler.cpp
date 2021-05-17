#include <algorithm>
#include <cassert>
#include <cstring>
#include <cctype>
#include <fcntl.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "HttpHandler.h"
#include "Utils.h"

// 声明一下该静态成员变量
string HttpHandler::www_path;

HttpHandler::HttpHandler(Epoll* epoll, int fd) 
    : client_fd_(fd), epoll_(epoll), isClosed_(false)
{
    // HTTP1.1下,默认是持续连接
    // 除非 client http headers 中带有 Connection: close
    isKeepAlive_ = true;
    // 如果先前没有设置 www 路径,则设置路径为当前的工作路径
    if(www_path.empty())
        setWWWPath(".");
}

HttpHandler::~HttpHandler()
{
    // 关闭客户套接字
    LOG(INFO) << "------------------ Connection Closed ------------------" << endl;
    close(client_fd_);
}

HttpHandler::ERROR_TYPE HttpHandler::readRequest()
{
    // 清除之前的数据
    request_.clear();
    pos_ = 0;

    char buffer[MAXBUF];
    
    // 循环非阻塞读取 ------------------------------------------
    for(;;)
    {
        ssize_t len = readn(client_fd_, buffer, MAXBUF, false, false);
        if(len < 0)
        {
            return ERR_READ_REQUEST_FAIL;
        }
        /** 
         * 如果此时没读取到信息并且之前已经读取过信息了,则直接返回.
         * 这里需要注意,有些连接可能会提前连接过来,但是不会马上发送数据.因此需要阻塞等待
         * 这里有个坑点: chromium在每次刷新过后,会额外开一个连接,用来缩短下次发送请求的时间
         * 也就是说这里大概率会出现空连接,即连接到了,但是不会马上发送数据,而是等下一次的请求.
         * 
         * 如果读取到的字节数为0,则说明远程连接已经被关闭.
         */
        else if(len == 0)
        {
            // 对于已经读取完所有数据的这种情况
            if(request_.length() > 0)
                // 直接停止读取
                break;
            // 如果此时既没读取到数据,之前的 request_也为空,则表示远程连接已经被关闭
            else
                return ERR_CONNECTION_CLOSED;
        }
        // LOG(INFO) << "Read data: " << buffer << endl;

        // 将读取到的数据组装起来
        string request(buffer, buffer + len);
        request_ += request;

        // 由于当前的读取方式为阻塞读取,因此如果读取到的数据已经全部读取完成,则直接返回
        if(static_cast<size_t>(len) < MAXBUF)
            break;
    }
    return ERR_SUCCESS;
}

HttpHandler::ERROR_TYPE HttpHandler::parseURI()
{
    if(request_.empty())   return ERR_BAD_REQUEST;

    size_t pos1, pos2;
    
    pos1 = request_.find("\r\n");
    if(pos1 == string::npos)    return ERR_BAD_REQUEST;
    string&& first_line = request_.substr(0, pos1);
    // a. 查找get
    pos1 = first_line.find(' ');
    if(pos1 == string::npos)    return ERR_BAD_REQUEST;
    method_ = first_line.substr(0, pos1);

    string output_method = "Method: ";
    if(method_ == "GET")
        output_method += "GET";
    else
        return ERR_NOT_IMPLEMENTED;
    LOG(INFO) << output_method << endl;

    // b. 查找目标路径
    pos1++;
    pos2 = first_line.find(' ', pos1);
    if(pos2 == string::npos)    return ERR_BAD_REQUEST;

    // 获取path时,注意去除 path 中的第一个斜杠
    pos1++;
    path_ = first_line.substr(pos1, pos2 - pos1);
    // 如果 path 为空,则添加一个 . 表示当前文件夹
    if(path_.length() == 0)
        path_ += ".";
    
    // 判断目标路径是否是文件夹
    struct stat st;
    if(stat(path_.c_str(), &st) == 0)
    {
        // 如果试图打开一个文件夹,则添加 index.html
        if (S_ISDIR(st.st_mode))
            path_ += "/index.html";
    }

    LOG(INFO) << "Path: " << path_ << endl;

    // c. 查看HTTP版本
    // NOTE 这里只支持 HTTP/1.0 和 HTTP/1.1
    pos2++;
    http_version_ = first_line.substr(pos2, first_line.length() - pos2);
    LOG(INFO) << "HTTP Version: " << http_version_ << endl;

    // 检测是否支持客户端 http 版本
    if(http_version_ != "HTTP/1.0" && http_version_ != "HTTP/1.1")
        return ERR_HTTP_VERSION_NOT_SUPPORTED;
    // 设置只在 HTTP/1.1时 允许 持续连接
    if(http_version_ != "HTTP/1.1")
        isKeepAlive_ = false;

    // 更新pos_
    pos_ = first_line.length() + 2;
    return ERR_SUCCESS;
}

HttpHandler::ERROR_TYPE HttpHandler::parseHttpHeader()
{
    // 清除之前的 http header
    headers_.clear();

    size_t pos1, pos2;
    for(pos1 = pos_;
        (pos2 = request_.find("\r\n", pos1)) != string::npos;
        pos1 = pos2 + 2)
    {
        string&& header = request_.substr(pos1, pos2 - pos1);
        // 如果遍历到了空头,则表示http header部分结束
        if(header.size() == 0)
            break;
        pos1 = header.find(' ');
        if(pos1 == string::npos)    return ERR_BAD_REQUEST;
        // key处减去1是为了消除key里的最后一个冒号字符
        string&& key = header.substr(0, pos1 - 1);
        // key 转小写
        transform(key.begin(), key.end(), key.begin(), ::tolower);
        // 获取 value
        string&& value = header.substr(pos1 + 1);

        LOG(INFO) << "HTTP Header: [" << key << " : " << value << "]" << endl;

        headers_[key] = value;
    }
    // 获取header完成后,处理一下 Connection 头
    auto conHeaderIter = headers_.find("connection");
    if(conHeaderIter != headers_.end())
    {
        string value = conHeaderIter->second;
        transform(value.begin(), value.end(), value.begin(), ::tolower);
        if(value != "keep-alive")
            isKeepAlive_ = false;
    }
    // 判断处理空 header 条目的 \r\n
    if((request_.size() < pos1 + 2) || (request_.substr(pos1, 2) != "\r\n"))
        return ERR_BAD_REQUEST;

    pos_ = pos1 + 2;
    return ERR_SUCCESS;
}



HttpHandler::ERROR_TYPE HttpHandler::sendResponse(const string& responseCode, const string& responseMsg, 
                            const string& responseBodyType, const string& responseBody)
{
    stringstream sstream;
    sstream << "HTTP/1.1" << " " << responseCode << " " << responseMsg << "\r\n";
    sstream << "Connection: " << (isKeepAlive_ ? "Keep-Alive" : "Close") << "\r\n";
    sstream << "Server: WebServer/1.0" << "\r\n";
    sstream << "Content-length: " << responseBody.size() << "\r\n";
    sstream << "Content-type: " << responseBodyType << "\r\n";
    sstream << "\r\n";
    sstream << responseBody;

    string&& response = sstream.str();
    ssize_t len = writen(client_fd_, (void*)response.c_str(), response.size());

    // 输出返回的数据
    LOG(INFO) << "<<<<- Response Packet ->>>> " << endl;
    LOG(INFO) << "{" << escapeStr(response, MAXBUF) << "}" << endl;

    if(len < 0 || static_cast<size_t>(len) != response.size())
        return ERR_SEND_RESPONSE_FAIL;
    return ERR_SUCCESS;
}

HttpHandler::ERROR_TYPE HttpHandler::handleError(const string& errCode, const string& errMsg)
{
    string errStr = errCode + " " + errMsg;
    string responseBody = 
                "<html>"
                "<title>" + errStr + "</title>"
                "<body>" + errStr + 
                    "<hr><em> Kiprey's Web Server</em>"
                "</body>"
                "</html>";
    return sendResponse(errCode, errMsg, "text/html", responseBody);
}

void HttpHandler::RunEventLoop()
{
    ERROR_TYPE err_ty;

    // 持续连接
    while(isKeepAlive_)
    {
        LOG(INFO) << "<<<<- Request Packet ->>>> " << endl;
        // 从socket读取请求数据, 如果读取失败,或者断开连接
        // NOTE 这里的 readRequest 必须完整读取整个 http 报文
        if((err_ty = readRequest()) != ERR_SUCCESS)
        {
            if(err_ty == ERR_READ_REQUEST_FAIL)
                LOG(ERROR) << "Read request failed ! " << strerror(errno) << endl;
            else if(err_ty == ERR_CONNECTION_CLOSED)
                LOG(INFO) << "Socket(" << client_fd_ << ") was closed." << endl;
            else
                assert(0 && "UNREACHABLE");       
            // 断开连接     
            break;
        }
        LOG(INFO) << "{" << escapeStr(request_, MAXBUF) << "}" << endl;
        
        // 解析信息 ------------------------------------------
        LOG(INFO) << "<<<<- Request Info ->>>> " << endl;

        // 1. 先解析第一行
        if((err_ty = parseURI()) != ERR_SUCCESS)
        {
            if(err_ty == ERR_NOT_IMPLEMENTED)
            {
                LOG(ERROR) << "Request method is not implemented." << endl;
                handleError("501", "Not Implemented");
            }
            else if(err_ty == ERR_HTTP_VERSION_NOT_SUPPORTED)
            {
                LOG(ERROR) << "Request HTTP Version Not Supported." << endl;
                handleError("505", "HTTP Version Not Supported");
            }
            else if(err_ty == ERR_BAD_REQUEST)
            {
                LOG(ERROR) << "Bad Request." << endl;
                handleError("400", "Bad Request");
            }
            else
                assert(0 && "UNREACHABLE"); 
            continue;
        }
        // 2. 解析每一条http header
        if((err_ty = parseHttpHeader()) != ERR_SUCCESS)
        {
            if(err_ty == ERR_BAD_REQUEST)
            {
                LOG(ERROR) << "Bad Request." << endl;
                handleError("400", "Bad Request");
            }
            else
                assert(0 && "UNREACHABLE"); 
            continue;
        }
        // 3. 输出剩余的 HTTP body
        LOG(INFO) << "HTTP Body: {" 
                << escapeStr(request_.substr(pos_, request_.length() - pos_), MAXBUF) 
                << "}" << endl;

        // 发送目标数据 ------------------------------------------

        // 试图打开一个文件
        int file_fd;
        if((file_fd = open(path_.c_str(), O_RDONLY, 0)) == -1)
        {
            // 如果打开失败,则返回404
            LOG(ERROR) << "File [" << path_ << "] open failed ! " << strerror(errno) << endl;
            handleError("404", "Not Found"); 
            continue;
        }  
        else
        {
            // 获取目标文件的大小
            struct stat st;
            if(stat(path_.c_str(), &st) == -1)
            {
                LOG(ERROR) << "Can not get file [" << path_ << "] state ! " << endl;
                handleError("500", "Internal Server Error");
                continue;
            }
            // 读取文件, 使用 mmap 来高速读取文件
            void* addr = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
            // 记得关闭文件描述符
            close(file_fd); 
            // 异常处理
            if(addr == MAP_FAILED)
            {
                LOG(ERROR) << "Can not map file [" << path_ << "] -> mem ! " << endl;
                handleError("500", "Internal Server Error");
                continue;
            }
            // 将数据从内存页存入至 responseBody
            char* file_data_ptr = static_cast<char*>(addr);
            string responseBody(file_data_ptr, file_data_ptr + st.st_size);
            // 记得删除内存
            int res = munmap(addr, st.st_size);
            if(res == -1)
                LOG(ERROR) << "Can not unmap file [" << path_ << "] <-> mem ! " << endl;
            // 获取 Content-type
            string suffix = path_;
            // 通过循环找到最后一个 dot
            size_t dot_pos;
            while((dot_pos = suffix.find('.')) != string::npos)
                suffix = suffix.substr(dot_pos + 1);

            // 发送数据
            if(sendResponse("200", "OK", MimeType::getMineType(suffix), responseBody) != ERR_SUCCESS)
                LOG(ERROR) << "Send Response failed !" << endl;
        }
    }
}