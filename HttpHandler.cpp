#include <arpa/inet.h>
#include <cassert>
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

HttpHandler::HttpHandler(int fd) : client_fd_(fd) 
{
    
}

HttpHandler::~HttpHandler()
{
    
}

void HttpHandler::printConnectionStatus()
{
    // 输出连接信息 [Server]IP:PORT <---> [Client]IP:PORT
    sockaddr_in serverAddr, peerAddr;
    socklen_t serverAddrLen, peerAddrLen;

    if((getsockname(client_fd_, (struct sockaddr *)&serverAddr, &serverAddrLen) != -1)
        && (getpeername(client_fd_, (struct sockaddr *)&peerAddr, &peerAddrLen) != -1))
        LOG(INFO) << "(socket: " << client_fd_ << ")" << "[Server] " << inet_ntoa(serverAddr.sin_addr) << ":" << ntohs(serverAddr.sin_port) 
              << " <---> [Client] " << inet_ntoa(peerAddr.sin_addr) << ":" << ntohs(peerAddr.sin_port) << endl;
}

void HttpHandler::readRequest()
{
    // 清除之前的数据
    request_.clear();
    char buffer[MAXBUF];
    // 设置 read 非阻塞读取. 注意套接字仍然是阻塞的,这是为了阻塞 accept 函数
    if(!setSocketNoBlock(client_fd_))
    {
        LOG(ERROR) << "Can not set socket " << client_fd_ << " No Block ! " << endl;
        return;
    }
    // 循环非阻塞读取 ------------------------------------------
    for(;;)
    {
        int len = readn(client_fd_, buffer, MAXBUF);
        if(len < 0)
        {
            LOG(ERROR) << "读取request异常" << endl;
            return;
        }
        // 如果已经全部读取完成
        else if(len == 0)
            break;
        // LOG(INFO) << "Read data: " << buffer << endl;

        // 将读取到的数据组装起来
        string request(buffer, buffer + len);
        request_ += request;
    }
}

size_t HttpHandler::parseURI()
{
    assert(!request_.empty());

    size_t pos1, pos2;
    
    pos1 = request_.find("\r\n");
    assert(pos1 != string::npos);
    string&& first_line = request_.substr(0, pos1);
    // a. 查找post/get
    pos1 = first_line.find(' ');
    assert(pos1 != string::npos);
    method_ = first_line.substr(0, pos1);

    string output_method = "Method: ";
    if(method_ == "POST")
        output_method += "POST";
    else if(method_ == "GET")
        output_method += "GET";
    else
        output_method += method_ + "(UNIMPLEMENTED)";
    LOG(INFO) << output_method << endl;

    // b. 查找目标路径
    pos1++;
    pos2 = first_line.find(' ', pos1);
    assert(pos2 != string::npos);

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
    pos2++;
    http_version_ = first_line.substr(pos2, first_line.length() - pos2);
    LOG(INFO) << "HTTP Version: " << http_version_ << endl;

    return first_line.length() + 2;
}

size_t HttpHandler::parseHttpHeader(size_t start_pos)
{
    // 清除之前的 http header
    headers_.clear();

    size_t pos1, pos2;
    for(pos1 = start_pos;
        (pos2 = request_.find("\r\n", pos1)) != string::npos;
        pos1 = pos2 + 2)
    {
        string&& header = request_.substr(pos1, pos2 - pos1);
        // 如果遍历到了空头,则表示http header部分结束
        if(header.size() == 0)
            break;
        pos1 = header.find(' ');
        assert(pos1 != string::npos);
        // key处减去1是为了消除key里的最后一个冒号字符
        string&& key = header.substr(0, pos1 - 1);
        string&& value = header.substr(pos1 + 1);

        LOG(INFO) << "HTTP Header: [" << key << " : " << value << "]" << endl;

        headers_[key] = value;
    }
    return pos1 + 2;
}

void HttpHandler::printStr(const string& str)
{
    string msg = str;
    // 遍历所有字符
    for(size_t i = 0; i < msg.length(); i++)
    {
        char ch = msg[i];
        if(iscntrl(ch))
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
                sprintf(hex, "\\x%02x", ch);
                substr = hex;
            }
            msg.replace(i, 1, substr);
        }
    }
    // 将读取到的数据输出
    if(msg.length() > MAXBUF)
        LOG(INFO) << "{" << msg.substr(0, MAXBUF) << " ... ... " << "}" << endl;
    else
        LOG(INFO) << "{" << msg << "}" << endl;

}

bool HttpHandler::sendResponse(const string& responseCode, const string& responseMsg, 
                            const string& responseBodyType, const string& responseBody)
{
    stringstream sstream;
    sstream << "HTTP/1.1" << " " << responseCode << " " << responseMsg << "\r\n";
    // 由于我们暂时实现的是无连接状态,因此带一个 ConnectionClose标志
    sstream << "Connection: close" << "\r\n";
    sstream << "Server: WebServer/1.0" << "\r\n";
    sstream << "Content-length: " << responseBody.size() << "\r\n";
    sstream << "Content-type: " << responseBodyType << "\r\n";
    sstream << "\r\n";
    sstream << responseBody;

    string&& response = sstream.str();
    ssize_t len = writen(client_fd_, (void*)response.c_str(), response.size());

    // 输出返回的数据
    LOG(INFO) << "<<<<- Response Packet ->>>> " << endl;
    printStr(response);

    return len >= 0 && static_cast<size_t>(len) != response.size();
}

bool HttpHandler::handleError(const string& errCode, const string& errMsg)
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
    LOG(INFO) << "------------------- New Connection -------------------" << endl;

    // 输出连接
    printConnectionStatus();

    LOG(INFO) << "<<<<- Request Packet ->>>> " << endl;
    // 从socket读取请求数据
    readRequest();
    printStr(request_);
    
    // 解析信息 ------------------------------------------
    LOG(INFO) << "<<<<- Request Info ->>>> " << endl;
    size_t pos;
    // 1. 先解析第一行
    pos = parseURI();
    // 2. 解析每一条http header
    pos = parseHttpHeader(pos);
    // 3. 输出剩余的 HTTP body
    LOG(INFO) << "HTTP Body: [" 
              << request_.substr(pos, request_.length() - pos) 
              << "]" << endl;

    // 发送目标数据 ------------------------------------------

    // 试图打开一个文件
    int file_fd;
    if((file_fd = open(path_.c_str(), O_RDONLY, 0)) == -1)
    {
        // 如果打开失败,则返回404
        LOG(ERROR) << "File [" << path_ << "] open failed ! ErrorCode: " << errno << endl;
        handleError("404", "Not Found");
    }  
    else
    {
        // 获取目标文件的大小
        struct stat st;
        if(stat(path_.c_str(), &st) == -1)
        {
            LOG(ERROR) << "Can not get file [" << path_ << "] state ! " << endl;
            return;
        }
        // 读取文件, 使用 mmap 来高速读取文件
        void* addr = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
        // 记得关闭文件描述符
        close(file_fd);
        // 异常处理
        if(addr == MAP_FAILED)
        {
            LOG(ERROR) << "Can not map file [" << path_ << "] -> mem ! " << endl;
            return;
        }
        // 将数据从内存页存入至 responseBody
        char* file_data_ptr = static_cast<char*>(addr);
        string responseBody(file_data_ptr, file_data_ptr + st.st_size);

        // 获取 Content-type
        string suffix = path_;
        // 通过循环找到最后一个 dot
        size_t dot_pos;
        while((dot_pos = suffix.find('.')) != string::npos)
            suffix = suffix.substr(dot_pos + 1);

        // 发送数据
        sendResponse("200", "OK", MimeType::getMineType(suffix), responseBody);
    }
    LOG(INFO) << "------------------ Connection Closed ------------------" << endl;
}