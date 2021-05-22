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
#include <sys/wait.h>
#include <unistd.h>

#include "HttpHandler.h"
#include "Utils.h"

// 声明一下该静态成员变量
 // 如果先前没有设置 www 路径,则设置路径为当前的工作路径
string HttpHandler::www_path = ".";

HttpHandler::HttpHandler(Epoll* epoll, int fd) 
    : client_fd_(fd), epoll_(epoll), curr_parse_pos_(0)
{
    // HTTP1.1下,默认是持续连接
    // 除非 client http headers 中带有 Connection: close
    isKeepAlive_ = true;
    // 初始化一些变量
    reset();
}

HttpHandler::~HttpHandler()
{
    // 从 epoll 中删除该套接字相关的事件
    /// NOTE: 注意先删除 epoll 中的条目,再来关闭 fd
    bool ret = epoll_->del(client_fd_);
    assert(ret);
    // 关闭客户套接字
    LOG(INFO) << "------------ Connection Closed (socket: " << client_fd_ << ")------------" << endl;
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
    againTimes_ = 0;
    // 重置 headers_
    headers_.clear();
    // 重置 body
    http_body_.clear();
}

HttpHandler::ERROR_TYPE HttpHandler::readRequest()
{
    LOG(INFO) << "<<<<- Request Packet ->>>> " << endl;

    char buffer[MAXBUF];
    
    while(true)
    {
        // 非阻塞,使用 recv 读取
        ssize_t len = readn(client_fd_, buffer, MAXBUF, false);
        if(len < 0)
            return ERR_READ_REQUEST_FAIL;
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
            // 读取时没有出错
            if(errno == EAGAIN)
                return ERR_SUCCESS;
            /** 
             * 否则,如果此时没读取到数据,则表示远程连接已经被关闭
             * 如果是正常断开连接,则此时的 errno 应该为 SUCCESS, 因为没有触发错误
             * 如果收到了远程的 RST,则 errno 为 ENOENT
             * NOTE: 需要注意的是, 当远程连接关闭时,有几率会导致下一次readn时仍然返回 EAGAIN
             *       即重复返回 EAGAIN,因此需要设置一个 againTime
             *       最好有个进一步的处理
             */
            assert(errno == 0 || errno == ENOENT); 
            // 不管怎样,直接关闭连接
            return ERR_CONNECTION_CLOSED;
        }
        // LOG(INFO) << "Read data: " << buffer << endl;

        // 将读取到的数据组装起来
        string request(buffer, buffer + len);
        LOG(INFO) << "{" << escapeStr(request, MAXBUF) << "}" << endl;

        request_ += request;
    }
    return ERR_SUCCESS;
}

HttpHandler::ERROR_TYPE HttpHandler::parseURI()
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
    LOG(INFO) << "Method: " << methodStr << endl;

    // b. 查找目标路径
    pos1++;
    pos2 = first_line.find(' ', pos1);
    if(pos2 == string::npos)    return ERR_BAD_REQUEST;

    // 获取path时,注意加上 www path
    path_ = www_path + first_line.substr(pos1, pos2 - pos1);
    
    LOG(INFO) << "Path: " << path_ << endl;

    // c. 查看HTTP版本
    pos2++;
    string http_version_str = first_line.substr(pos2, first_line.length() - pos2);
    LOG(INFO) << "HTTP Version: " << http_version_str << endl;

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

HttpHandler::ERROR_TYPE HttpHandler::parseHttpHeader()
{
    LOG(INFO) << "<<<<- Request Info ->>>> " << endl;

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
        if(header[pos1 - 1] != ':') return ERR_BAD_REQUEST;

        // key处减去1是为了消除key里的最后一个冒号字符
        string&& key = header.substr(0, pos1 - 1);
        // key 转小写
        transform(key.begin(), key.end(), key.begin(), ::tolower);
        // 获取 value
        string&& value = header.substr(pos1 + 1);

        LOG(INFO) << "HTTP Header: [" << key << " : " << value << "]" << endl;

        headers_[key] = value;
    }
    // // 判断处理空 header 条目的 \r\n
    // if((request_.size() < pos1 + 2) || (request_.substr(pos1, 2) != "\r\n"))
    //     return ERR_BAD_REQUEST;

    // 执行到这里说明: 没有遍历到空头,即还有数据没有读完
    return ERR_AGAIN;
}

HttpHandler::ERROR_TYPE HttpHandler::parseBody()
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
    LOG(INFO) << "HTTP Body: {" << escapeStr(http_body_, MAXBUF) << "}" << endl;

    return ERR_SUCCESS;    
}

HttpHandler::ERROR_TYPE HttpHandler::handleRequest()
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

    // 获取目标文件的信息
    struct stat st;
    if(stat(path_.c_str(), &st) == -1)
    {
        if(errno == ENOENT)
        {
            LOG(ERROR) << "file [" << path_ << "] not found." << endl;
            return ERR_NOT_FOUND;
        }
        else
        {
            LOG(ERROR) << "Can not get file [" << path_ << "] state ! " 
                << strerror(errno) << endl;
            return ERR_INTERNAL_SERVER_ERR;
        }
    }

    // 开始处理请求
    // 对于普通的 GET / HEAD 请求,读取文件并发送
    if(method_ == METHOD_GET || method_ == METHOD_HEAD)
    {
        // 试图打开一个文件
        int file_fd;
        if((file_fd = open(path_.c_str(), O_RDONLY, 0)) == -1)
        {
            // 如果打开失败,则返回404
            LOG(ERROR) << "File [" << path_ << "] open failed ! " << strerror(errno) << endl;
            return ERR_NOT_FOUND;
        }  
        // 读取文件, 使用 mmap 来高速读取文件
        void* addr = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
        // 记得关闭文件描述符
        close(file_fd); 
        // 异常处理
        if(addr == MAP_FAILED)
        {
            LOG(ERROR) << "Can not map file [" << path_ << "] -> mem ! " << endl;
            return ERR_INTERNAL_SERVER_ERR;
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

        // 发送数据, 在该函数内部, METHOD_HEAD 不发送 http body
        return sendResponse("200", "OK", MimeType::getMineType(suffix), responseBody);
    }
    // 而对于POST来说,将 http body 传入目标可执行文件并将结果返回给客户端
    /**
     * @brief 多进程调试
     *  gdb: set follow-fork-mode parent
     *       set detach-on-fork off
     *  shell: 
     *       查看某个进程的pid:          ps ax | grep "WebServer" 
     *       查看某个pid的文件描述符列表:  lsof -p <PID>
     *       查看WebServer的所有子进程:  pstree -p -g <WebServerPID>
     */
    else if(method_ == METHOD_POST)
    {
        // 创建两个管道
        int cgi_output[2];
        int cgi_input[2];
        if (pipe(cgi_output) == -1) {
            LOG(ERROR) << "cgi_output create error. " << strerror(errno) << endl;
            return ERR_INTERNAL_SERVER_ERR;
        }
        if (pipe(cgi_input) == -1) {
            LOG(ERROR) << "cgi_output create error. " << strerror(errno) << endl;
            return ERR_INTERNAL_SERVER_ERR;
        }
        // 尝试执行该CGI程序
        pid_t pid;
        /**
         * @note 需要注意的是 fork 在多进程中要慎重使用
         * @ref 谨慎使用多线程中的fork https://www.cnblogs.com/liyuan989/p/4279210.html
         * @ref 程序员的自我修养（三）：fork() 安全 https://liam.page/2017/01/17/fork-safe/
         */ 
        if((pid = fork()) < 0)
        {
            LOG(ERROR) << "Fork error. " << strerror(errno) << endl;
            return ERR_INTERNAL_SERVER_ERR;
        }
        // 对于子进程来说
        if(pid == 0)
        {
            /**
             * 将当前进程的进程号设置为所在组的进程组的组号
             * 这有助于WebServer 杀死子进程
             * 
             * kill -pid 时会杀死 PGID为 `-pid` 的所有子进程
             * 因此可以利用 setpgid 来达到区分进程的目的
             * 
             * 正常来说,如果没有设置 setpgid,则WebServer所有的子进程,以及子进程的子进程
             * 其PGID都为WebServer的PID,这为杀死 pid为某个特定值的子进程以及该子进程的子进程巨大障碍
             * 因此在子进程处需要重新设置 pgid
             */
            // 正常来说, setpgid 不可能会失败.如果失败了就直接abort
            // 因为设置失败将会导致该子进程无法受到父进程的超时限制
            if(setpgid(0, 0))
                abort();
            // 首先重新设置标准输入输出流
            if(dup2(cgi_input[0], 0) == -1 || dup2(cgi_output[1], 1) == -1)
                exit(EXIT_FAILURE);
            close(cgi_input[0]);
            close(cgi_input[1]);
            close(cgi_output[0]);
            close(cgi_output[1]);

            // 执行
            const char* path = path_.c_str();
            // 注意,由于不需要从path中获取文件地址,因此这里使用的exec函数不是p系列的
            execl(path, path, nullptr);
            exit(EXIT_FAILURE);
        }
        // 对于父进程WebServer来说
        else
        {
            close(cgi_input[0]);
            close(cgi_output[1]);

            ssize_t len = writen(cgi_input[1], http_body_.c_str(), http_body_.length(), true);
            // 这里确保写入正确
            assert(len >= 0 && static_cast<size_t>(len) == http_body_.length());

            close(cgi_input[1]);

            // 设置超时时间 maxCGIRuntime(ms)
            int timeouts = maxCGIRuntime;
            /**
             * @brief 进入一个死循环,只有当子进程退出后才会break
             * @note 该循环将会有2条执行流程
             *          1. 执行子进程 -> waitpid -> 子进程退出 -> 结束循环;
             *          2. 执行子进程 -> waitpid -> 子进程没有退出
             *              -> 超时 -> kill -> waitpid -> 子进程退出 -> 结束循环;
             */
            while(true)
            {
                // 单次休息 cgiStepTime(ms)
                if(!usleep(cgiStepTime * 1000))
                    timeouts -= cgiStepTime;
                int wstats = -1;
                int waitpid_ret = waitpid(pid, &wstats, WNOHANG);
                // 如果waitpid 出错
                if(waitpid_ret < 0)
                {
                    LOG(ERROR) << "waitpid error. " << strerror(errno) << endl;
                    // ret 前,一定一定一定要关闭这个读取端口
                    close(cgi_output[0]);
                    return ERR_INTERNAL_SERVER_ERR;
                }
                // 如果子进程状态被修改, 当子进程状态改变后,waitpid 才会设置 status, 否则 status 不变
                else if(waitpid_ret > 0)
                {
                    // 只有在子进程自然退出,或者子进程被 kill 时,才会处理,退出该循环
                    // 至于其他情况,例如子进程遇到了 SIGINT,则忽视
                    bool ifExited = WIFEXITED(wstats);
                    // 注意 SIGKILL 会 terminate 子进程,因此使用 WTERMSIG 来获取TERSIG
                    // 这里不指定是 SIGKILL信号,因为可能有其他信号会kill子进程
                    bool ifKilled = WIFSIGNALED(wstats) && (WTERMSIG(wstats) != 0);
                    if(ifExited || ifKilled)
                        break;
                }
                // 如果什么也没有发生, 即子进程仍然在跑.如果顺便超时了,则kill
                else if(timeouts <= 0)
                {
                    /** 
                     * @brief 把 kill 放到循环内部是为了 waitpid 回收子进程
                     * NOTE: -pid 指的是杀死当前子进程以及该子进程自身的子进程,例如shell脚本
                     * NOTE: 再kill一次 pid 是为了防止子进程太久没有轮到执行,仍然处于fork与execl之间的状态
                     *       此时,之前的 kill -pid 将会不起作用.因此为了确保子进程一定被kill,需要再kill一次pid
                     */        
                    int res_kill_sub = kill(pid, SIGKILL);
                    int res_kill_pgid = 0;
                    // 只有在子进程的pgid变化后才kill -pid,防止误伤其他线程中的子进程
                    if(getpgid(pid) == pid)
                        res_kill_pgid = kill(-pid, SIGKILL);
                    assert(!res_kill_sub && !res_kill_pgid);
                    LOG(ERROR) << "Sub process timeout." << endl;
                }
            }

            string responseBody;
            char buf[MAXBUF];
            // 非阻塞读取
            if(!setFdNoBlock(cgi_output[0]))
            {
                LOG(ERROR) << "set fd(" << cgi_output[0] << ") no block fail! " << strerror(errno) << endl;
                close(cgi_output[0]);
                return ERR_INTERNAL_SERVER_ERR;
            }
            while((len = readn(cgi_output[0], buf, MAXBUF, true)) > 0)
                responseBody += string(buf, buf + len);
            close(cgi_output[0]);

            if(responseBody.empty())
                return ERR_INTERNAL_SERVER_ERR;
            // 发送数据
            return sendResponse("200", "OK", MimeType::getMineType("txt"), responseBody);
        }
    }
    else
        return ERR_INTERNAL_SERVER_ERR;
    UNREACHABLE();
    return ERR_SUCCESS;
}

bool HttpHandler::handleErrorType(HttpHandler::ERROR_TYPE err)
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
        LOG(ERROR) << "HTTP Read request failed ! " << strerror(errno) << endl;
        state_ = STATE_FATAL_ERROR;
        break;
    case ERR_AGAIN:
        ++againTimes_;
        LOG(INFO) << "HTTP waiting for more messages... " << endl;
        /* 注意这里没有设置 STATE , 与 ERR_SUCESS一样 */
        if(againTimes_ > maxAgainTimes)
        {
            state_ = STATE_FATAL_ERROR;
            LOG(ERROR) << "Reach max read times" << endl;
        }
        break;
    case ERR_CONNECTION_CLOSED:
        LOG(INFO) << "HTTP Socket(" << client_fd_ << ") was closed." << endl;
        state_ = STATE_FATAL_ERROR;
        break;
    case ERR_SEND_RESPONSE_FAIL:
        LOG(ERROR) << "Send Response failed !" << endl;
        state_ = STATE_FATAL_ERROR;
        break;
    case ERR_BAD_REQUEST:
        LOG(ERROR) << "HTTP Bad Request." << endl;
        sendErrorResponse("400", "Bad Request");
        state_ = STATE_ERROR;
        break;
    case ERR_NOT_FOUND:
        LOG(ERROR) << "HTTP Not Found." << endl;
        sendErrorResponse("404", "Not Found");
        state_ = STATE_ERROR;
        break;
    case ERR_LENGTH_REQUIRED:
        LOG(ERROR) << "HTTP Length Required." << endl;
        sendErrorResponse("411", "Length Required");
        state_ = STATE_ERROR;
        break;
    case ERR_NOT_IMPLEMENTED:
        LOG(ERROR) << "HTTP Request method is not implemented." << endl;
        sendErrorResponse("501", "Not Implemented");
        state_ = STATE_ERROR;
        break;
    case ERR_INTERNAL_SERVER_ERR:
        sendErrorResponse("500", "Internal Server Error");
        state_ = STATE_ERROR;
        break;
    case ERR_HTTP_VERSION_NOT_SUPPORTED:
        LOG(ERROR) << "HTTP Request HTTP Version Not Supported." << endl;
        sendErrorResponse("505", "HTTP Version Not Supported");
        state_ = STATE_ERROR;
        break;
    default:
        UNREACHABLE();
    }
    return isSuccess;
}

HttpHandler::ERROR_TYPE HttpHandler::sendResponse(const string& responseCode, const string& responseMsg, 
                            const string& responseBodyType, const string& responseBody)
{
    stringstream sstream;
    sstream << "HTTP/1.1" << " " << responseCode << " " << responseMsg << "\r\n";
    sstream << "Connection: " << (isKeepAlive_ ? "Keep-Alive" : "Close") << "\r\n";
    sstream << "Server: WebServer/1.1" << "\r\n";
    sstream << "Content-length: " << responseBody.size() << "\r\n";
    sstream << "Content-type: " << responseBodyType << "\r\n";
    sstream << "\r\n";
    // 如果是 HEAD 请求,则不发送 http body
    if(method_ != METHOD_HEAD)
        sstream << responseBody;

    string&& response = sstream.str();
    /// TODO: 尽管writen是阻塞写入,但需要注意的是,阻塞写入可能会极大影响Server性能,因此最好使用 epoll 来写入
    ssize_t len = writen(client_fd_, (void*)response.c_str(), response.size());

    // 输出返回的数据
    LOG(INFO) << "<<<<- Response Packet ->>>> " << endl;
    LOG(INFO) << "{" << escapeStr(response, MAXBUF) << "}" << endl;

    if(len < 0 || static_cast<size_t>(len) != response.size())
        return ERR_SEND_RESPONSE_FAIL;
    return ERR_SUCCESS;
}

HttpHandler::ERROR_TYPE HttpHandler::sendErrorResponse(const string& errCode, const string& errMsg)
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

bool HttpHandler::RunEventLoop()
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
    bool ret = epoll_->modify(client_fd_, this, EPOLLET | EPOLLIN | EPOLLONESHOT | EPOLLRDHUP | EPOLLHUP);
    assert(ret);
    return true;
}