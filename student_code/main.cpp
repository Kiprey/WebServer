#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <rapidjson/document.h>

#include "Epoll.h"
#include "HttpHandler.h"
#include "Log.h"
#include "ThreadPool.h"
#include "Utils.h"
#include "Database.h"

enum TASK_PRIORITY {
    PARSE_HTTP_REQUEST = 0, // lowest priority
    QUERY_DATABASE = 1,     // middle priority
    FLUSH_DATABASE = 2,     // highest priority
};


/**
 * @brief 处理旧的连接
 * @param event         待处理的事件
 * @param fd            被唤醒的文件描述符
 */
bool handle_potential_epoll_error(epoll_event event, int fd)
{
    // 处理一些错误事件
    int events_ = event.events;
    // 如果远程关闭了当前连接
    if ((events_ & EPOLLHUP) || (events_ & EPOLLRDHUP)) {
        DEBUG_INFO("Socket(%d) was closed by peer.", fd);
        // 当某个 handler 无法使用时,一定要销毁内存
        return false;
    }
    // 如果当前 socket / events_ 存在错误
    else if ((events_ & EPOLLERR) || !(events_ & EPOLLIN)) {
        ERROR("Socket(%d) error.", fd);
        // 当某个 handler 无法使用时,一定要销毁内存
        // 之后重新开始遍历新的事件.
        return false;
    }
    // 如果没有错误发生
    return true;
}

/**
 * @brief 处理新的连接
 * @param epoll     存放新连接的Epoll类实例 
 * @param listen_fd 新连接所对应的 listen 描述符
 */ 
void handleNewConnections(
    std::shared_ptr<HttpHandlerRegistry> http_handler_registry, 
    std::shared_ptr<Epoll> epoll, 
    std::shared_ptr<ThreadPool> thread_pool, 
    int listen_fd, 
    int* idle_fd, 
    std::shared_ptr<Router> router
)
{
    // 注意:可能会有很多个 connect 动作,但只会有一个 event
    sockaddr_in client_addr;
    socklen_t client_addr_len = 0;
    
    /**
     *  如果 
     *      1. accept 没有发生错误
     *      2. accppt 发生了 EINTR 错误
     *      3. accept 发生了 ECONNABORTED 错误(该错误是远程连接被中断)
     *  则重新循环. 其中第三点, 若发生了 aborted 错误,则继续循环接受下一个socket 的请求
     */
    for(;;) {
        int client_fd = accept4(listen_fd, (sockaddr*)&client_addr, &client_addr_len, 
                SOCK_NONBLOCK | SOCK_CLOEXEC);
        // accept 的错误处理
        if(client_fd == -1) {
            // 如果是因为一些无关的错误所阻断，则继续 accept
            if(errno == EINTR || errno == ECONNABORTED)
                continue;
            // 正常情况下,如果处理了所有的 accept后, errno == EAGAIN，则直接退出
            else if (errno == EAGAIN)
                break;
            // 如果由于文件描述符不够用了,则会返回 EMFILE，此时清空全部的尚未 accept 连接
            else if(errno == EMFILE) {
                int closed_conn_num = closeRemainingConnect(listen_fd, idle_fd);
                WARN("No reliable pipes in new connection, close %d conns", closed_conn_num);
                break;
            }
            // 如果是其他的错误，则输出信息
            else 
                ERROR("Accept Error! (%s)", strerror(errno));
        }
        // 如果 accept 正常
        else {
            /** 构建一个新的 HttpHandler,并放入 epoll 实例中
             *  注意这里使用了 ONESHOT, 每个套接字只会在 边缘触发,可读时处于就绪状态
             *  且每个套接字只会被一个线程处理
             *  NOTE: 每个 client_fd 只会在 HttpHandler 中被 close + 下面的 timer 异常处理中被关闭
             *        每个 Timer 在此处创建, 在 HttpHandler 中被释放
             */
            std::unique_ptr<Timer> timer = std::make_unique<Timer>(TFD_NONBLOCK | TFD_CLOEXEC);
            // 如果timer创建失败,则清空当前所有尚未 accept 的连接，因为文件描述符满
            if(!timer->isValid())
            {
                // 直接关闭，告诉远程这里放不下了
                close(client_fd);
                
                int closed_conn_num = closeRemainingConnect(listen_fd, idle_fd);
                WARN("No reliable pipes in new connection, close %d conns", closed_conn_num);
                break;
            }
            std::shared_ptr<HttpHandler> client_handler = std::make_shared<HttpHandler>(epoll, client_fd, std::move(timer), router);
            // 注册进全局的 http_handlers 中
            http_handler_registry->addHandler(client_handler->getClientFd(), client_handler);

            // 准备两个 weak_ptr
            std::weak_ptr<HttpHandler> weak_handler = client_handler;
            std::weak_ptr<HttpHandlerRegistry> weak_registry = http_handler_registry;

            // 创建一个自毁装置
            std::function<void(void)> close_callback = [epoll, weak_handler, weak_registry]() {
                // 从 epoll 中删除该套接字相关的事件
                /// NOTE: 注意先删除 epoll 中的条目,再来关闭 fd
                int client_fd = -1;
                {
                    std::shared_ptr<HttpHandler> shared_handler = weak_handler.lock();
                    assert(shared_handler);
                    client_fd = shared_handler->getClientFd();
                    bool ret1 = epoll->del(client_fd);
                    bool ret2 = epoll->del(shared_handler->getTimerFd());
                    assert(ret1 && ret2);
                }

                // 最后抹去它的存在，调用析构函数
                auto shared_handlers = weak_registry.lock();
                assert (shared_handlers && client_fd > 0);
                shared_handlers->removeHandler(client_fd);
            };

            // 准备两个 fd 的 EpollEvent
            EpollEventCallback timerfd_callback = [weak_handler](epoll_event event) mutable {
                std::shared_ptr<HttpHandler> shared_handler = weak_handler.lock();
                assert (shared_handler);
                if (handle_potential_epoll_error(event, shared_handler->getTimerFd())) {
                    DEBUG_INFO("-------->>>>> "
                        "New Message: socket(%d) - timerfd(%d) timeout."
                        " <<<<<--------",
                        shared_handler->getClientFd(), shared_handler->getTimerFd());
                }
                // 无论如何，都会删除 handler 实例
                shared_handler->destructNow();
            };

            EpollEventCallback client_callback = [epoll, weak_handler, thread_pool](epoll_event event) mutable {
                std::shared_ptr<HttpHandler> shared_handler = weak_handler.lock();
                assert (shared_handler);
                if (!handle_potential_epoll_error(event, shared_handler->getTimerFd())) {
                    shared_handler->destructNow();
                    return;
                }
                // 则从epoll中关闭 timer, 防止条件竞争
                bool ret = epoll->modify(shared_handler->getTimerFd(), nullptr, 0);
                assert (ret);
                // 并将其放入线程池中并行执行
                thread_pool->appendTask(
                    // lambda 函数
                    [epoll, weak_handler](void* arg) mutable
                    {
                        std::shared_ptr<HttpHandler> shared_handler = weak_handler.lock();
                        printConnectionStatus(shared_handler->getClientFd(), "-------->>>>> New Message");

                        // 如果出现无法恢复的错误,则直接释放该实例以及对应的 client_fd
                        if (shared_handler->RunEventLoopAndReEpoll()) {
                            bool ret1 = epoll->modify(shared_handler->getTimerFd(), shared_handler->getTimerEpollEventCallback(), shared_handler->getTimerTriggerCond());
                            bool ret2 = epoll->modify(shared_handler->getClientFd(), shared_handler->getClientEpollEventCallback(), shared_handler->getClientTriggerCond());
                            assert(ret1 && ret2);
                        }
                        else {
                            shared_handler->destructNow();
                        }
                    }, 
                    nullptr,
                    TASK_PRIORITY::PARSE_HTTP_REQUEST);
            };
            client_handler->setDestructor(std::move(close_callback));
            client_handler->setClientEpollEventCallback(std::move(client_callback));
            client_handler->setTimerEpollEventCallback(std::move(timerfd_callback));

            /**
             * @brief EPOLLRDHUP EPOLLHUP 不同点,前者是半关闭连接时出发,后者是完全关闭后触发
             * @ref tcp 源码 https://elixir.bootlin.com/linux/v4.19/source/net/ipv4/tcp.c#L524
             * @ref TCP: When is EPOLLHUP generated? https://stackoverflow.com/questions/52976152/tcp-when-is-epollhup-generated
             */ 
            bool ret1 = epoll->add(client_fd, client_handler->getClientEpollEventCallback(), client_handler->getClientTriggerCond());
            // 设置定时器以边缘-单次触发方式
            bool ret2 = epoll->add(client_handler->getTimerFd(), client_handler->getTimerEpollEventCallback(), client_handler->getTimerTriggerCond());
            assert(ret1 && ret2);
            // 输出相关信息
            printConnectionStatus(client_fd, "-------->>>>> New Connection");
        }
    }
}

// PGconn* connect_database() {
//     // "dbname=test user=postgres password=secret hostaddr=127.0.0.1 port=5432"
//     const char* conninfo = getenv("DATABASE_INFO");
//     if (!conninfo) {
//         DEBUG_INFO("DATABASE_INFO not set, falling back to POSTGRES_*");

//         const char* user = getenv("POSTGRES_USER");
//         const char* password = getenv("POSTGRES_PASSWORD");
//         const char* dbname = getenv("POSTGRES_DB");
//         const char* host = getenv("POSTGRES_HOST");
//         const char* port = getenv("POSTGRES_PORT");

//         if (!user || !password || !dbname || !host || !port)
//             return nullptr;

//         // 针对 PQconnectdb 无法自动处理主机名的缓解措施
//         std::string resolved_ip = resolve_hostname_to_ip(host);
//         if (resolved_ip.empty()) {
//             ERROR("Failed to resolve hostname: %s", host);
//             return nullptr;
//         }

//         char default_conninfo[512];
//         snprintf(default_conninfo, sizeof(default_conninfo), "dbname=%s user=%s password=%s host=%s port=%s",
//                  dbname, user, password, resolved_ip.c_str(), port);
        
//         conninfo = default_conninfo;
//     }


//     // 连接到 PostgreSQL 数据库
//     PGconn* pg_conn = PQconnectdb(conninfo);
//     if(PQstatus(pg_conn) != CONNECTION_OK)
//     {
//         ERROR("Connection to database failed: %s", PQerrorMessage(pg_conn));
//         PQfinish(pg_conn);
//         return nullptr;
//     }
//     return pg_conn;
// }

HTTP_ERROR_TYPE POST_api_bind(HttpHandler* handler)
{
    // string content_type;
    // if(handler->getHttpHeader("content-type", content_type) != ERR_SUCCESS)
    //     return ERR_BAD_REQUEST;
    // // 我们只支持 JSON 格式数据
    // if(content_type.find("json") == string::npos)
    //     return ERR_NOT_IMPLEMENTED;
    // // 解析 body
    // string body;
    // if(handler->getHttpBody(body) != ERR_SUCCESS)
    //     return ERR_BAD_REQUEST;
    // // 解析 JSON
    // Document document;
    // if (document.Parse(body.c_str()).HasParseError())
    //     return handler->sendErrorResponse("400", "Post Data is not a valid Json type");
    // // 判断document是否是一个对象
    // if (!document.IsObject())
    //     return handler->sendErrorResponse("400", "Document is not a valid object");
    // // 检查是否包含deviceid字段，并且它的值是一个字符串
    // if (!document.HasMember("deviceid") || !document["deviceid"].IsString())
    //     return handler->sendErrorResponse("400", "deviceid not found or not a string type");
    // string device_id = document["deviceid"].GetString();

    // // 检查是否为空或长度不为36
    // if(device_id.empty() || device_id.size() != 36)
    //     return handler->sendResponse("200", "OK", "application/json", R"({"code": 104})");

    // // query
    // const char* query = 
    //     "WITH ins AS ("
    //     "    INSERT INTO user_info (deviceid) "
    //     "    VALUES ($1) "
    //     "    ON CONFLICT (deviceid) "
    //     "    DO UPDATE SET deviceid = EXCLUDED.deviceid "
    //     "    RETURNING userid, CASE WHEN xmax = 0 THEN 'new' ELSE 'conflict' END AS status "
    //     ") "
    //     "SELECT * FROM ins;";

    // // 使用 PQexecParams 执行参数化查询
    // PGconn* pg_conn = connect_database();
    // if (!pg_conn)
    //     return ERR_NOT_IMPLEMENTED;

    // const char* paramValues[1] = { device_id.c_str() };
    // PGresult* res = PQexecParams(pg_conn, query, 1, nullptr, paramValues, nullptr, nullptr, 0);

    // if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    //     // 获取错误信息
    //     char err_msg[0x100];
    //     snprintf(err_msg, sizeof(err_msg), "PostgreSQL query failed: %s", PQresultErrorMessage(res));
    //     PQclear(res);
    //     PQfinish(pg_conn);
    //     return handler->sendErrorResponse("500", err_msg);
    // }

    // // 获取返回的 userid
    // const char* userid = PQgetvalue(res, 0, 0);
    // const char* status = PQgetvalue(res, 0, 1);
    // if (userid == nullptr || status == nullptr) {
    //     PQclear(res);
    //     PQfinish(pg_conn);
    //     return ERR_INTERNAL_SERVER_ERR;
    // }
    // string response = !strcmp(status, "new") ? 
    //     "{\"code\": 100, \"userid\": " + string(userid) + "}" :
    //     "{\"code\": 102, \"userid\": " + string(userid) + "}";

    // PQclear(res);
    // PQfinish(pg_conn);

    // return handler->sendResponse("200", "OK", "application/json", response);
}

HTTP_ERROR_TYPE POST_api_upload(HttpHandler* handler)
{
    // string content_type;
    // if(handler->getHttpHeader("content-type", content_type) != ERR_SUCCESS)
    //     return ERR_BAD_REQUEST;
    // // 我们只支持 JSON 格式数据
    // if(content_type.find("json") == string::npos)
    //     return ERR_NOT_IMPLEMENTED;
    // // 解析 body
    // string body;
    // if(handler->getHttpBody(body) != ERR_SUCCESS)
    //     return ERR_BAD_REQUEST;
    // // 解析 JSON
    // Document document;
    // if (document.Parse(body.c_str()).HasParseError())
    //     return handler->sendErrorResponse("400", "Post Data is not a valid Json type");
    // // 判断document是否是一个对象
    // if (!document.IsObject())
    //     return handler->sendErrorResponse("400", "Document is not a valid object");
    // // 检查是否包含 userid 和 data 字段，并且它的类型符合
    // if (!document.HasMember("data") || !document["data"].IsString() || !document.HasMember("userid") || !document["userid"].IsInt())
    //     return handler->sendErrorResponse("400", "data/userid not found or wrong type");
    // string data = document["data"].GetString();
    // int userid = document["userid"].GetInt();

    // // 检查是否为空或长度大于 256，VarChar(256)
    // if(data.empty() || data.size() > 256)
    //     return handler->sendResponse("200", "OK", "application/json", R"({"code": 104})");

    // // NOTE: 注意这里没有校验用户是否已经存在，因为 PDF 里没写
    // const char* query = "INSERT INTO user_data (userid, data) VALUES ($1, $2) "
    //                     "ON CONFLICT (userid) "
    //                     "DO UPDATE SET data = EXCLUDED.data";

    // PGconn* pg_conn = connect_database();
    // if (!pg_conn)
    //     return ERR_NOT_IMPLEMENTED;
    // // 使用 PQexecParams 执行参数化查询
    // const char* paramValues[2] = { to_string(userid).c_str(), data.c_str() };
    // PGresult* res = PQexecParams(pg_conn, query, 2, nullptr, paramValues, nullptr, nullptr, 0);

    // // 检查查询是否成功
    // if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    //     // 获取错误信息
    //     char err_msg[0x100];
    //     snprintf(err_msg, sizeof(err_msg), "PostgreSQL query failed: %s", PQresultErrorMessage(res));
    //     PQclear(res);
    //     PQfinish(pg_conn);
    //     return handler->sendErrorResponse("500", err_msg);
    // }

    // PQclear(res);
    // PQfinish(pg_conn);

    // return handler->sendResponse("200", "OK", "application/json", "{\"code\": 100 }");
}

int main(int argc, char* argv[])
{
    // 获取传入的参数
    if (argc < 2 || !isNumericStr(argv[1])) 
    {
        ERROR("usage: %s <port> [<www_dir>]", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    if(argc > 2)
        HttpHandler::setWWWPath(argv[2]);
    // 输出当前进程的 PID，便于调试
    INFO("PID: %d", getpid());
    // 忽略 SIGPIPE 信号
    handleSigpipe();
    // 创建线程池
    const char* thread_pool_size_env = getenv("THREAD_POOL_SIZE");
    if (!thread_pool_size_env) {
        ERROR("THREAD_POOL_SIZE not set");
        return false;
    }
    int thread_pool_size = std::stoi(thread_pool_size_env);
    if (thread_pool_size < 1 || thread_pool_size > 1024) {
        ERROR("Wrong THREAD_POOL_SIZE: %s", thread_pool_size_env);
        exit(EXIT_FAILURE);
    }
    std::shared_ptr<ThreadPool> thread_pool = std::make_shared<ThreadPool>(thread_pool_size);
    INFO("Thread Pool is started with %d threads", thread_pool_size);

    // 连接数据库
    const char* db_user = getenv("SERVER_POSTGRES_USER");
    const char* db_password = getenv("SERVER_POSTGRES_PASSWORD");
    const char* db_dbname = getenv("SERVER_POSTGRES_DB");
    const char* db_host = getenv("SERVER_POSTGRES_HOST");
    const char* db_port = getenv("SERVER_POSTGRES_PORT");

    if (!db_user || !db_password || !db_dbname || !db_host || !db_port) {
        ERROR("SERVER_POSTGRES_USER, SERVER_POSTGRES_PASSWORD, SERVER_POSTGRES_DB, SERVER_POSTGRES_HOST, SERVER_POSTGRES_PORT must be set");
        return false;
    }
    std::unique_ptr<Timer> db_timer = std::make_unique<Timer>(TFD_NONBLOCK | TFD_CLOEXEC);
    assert(db_timer->isValid());
    std::shared_ptr<ConnectionPool> db_conn_pool = std::make_shared<ConnectionPool>
        (db_host, db_port, db_user, db_password, db_dbname, thread_pool_size);
    std::shared_ptr<DBPipeline> query_buffer = std::make_shared<DBPipeline> (db_conn_pool, std::move(db_timer));
    INFO("Database Connection Pool is started with %d connection", thread_pool_size);

    // 空闲 fd，用于关闭溢出的文件描述符
    int idle_fd = open("/dev/null", O_RDONLY | O_CLOEXEC); 
    int listen_fd = -1;
    if((listen_fd = socket_bind_and_listen(port)) == -1)
    {
        ERROR("Bind %d port failed ! (%s)", port, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // 注册 Post Router
    std::shared_ptr<Router> router = std::make_shared<Router>();
    router->registerRoute("/api/bind", POST_api_bind);
    router->registerRoute("/api/upload", POST_api_upload);

    // 声明一个 epoll 实例,该实例将在整个main函数结束时被释放
    std::shared_ptr<Epoll> epoll = std::make_shared<Epoll>(EPOLL_CLOEXEC);
    assert(epoll->isEpollValid());
    
    std::shared_ptr<HttpHandlerRegistry> http_handler_registry = std::make_shared<HttpHandlerRegistry>();

    EpollEventCallback listen_epollevent = [epoll, thread_pool, http_handler_registry, listen_fd, &idle_fd, router](epoll_event event) {
        // 在 lambda 中调用 handleNewConnections
        handleNewConnections(http_handler_registry, epoll, thread_pool, listen_fd, &idle_fd, router);
    };
    // 将 listen_fd 添加进 epoll 实例
    epoll->add(listen_fd, &listen_epollevent, EPOLLET | EPOLLIN);

    // 开始事件循环
    for(;;)
    {
        // 阻塞等待新的事件
        int event_num = epoll->wait(-1);
        // 如果报错
        if(event_num < 0)
        {
            // 表示该错误一定不是因为无效的 epoll 导致的
            assert(event_num != -2);
            // 如果只是中断,则直接重新循环
            if(errno == EINTR)
                continue;
            // 如果是其他异常,则输出信息并终止.
            else
                FATAL("epoll_wait fail! (%s)", strerror(errno));
        }
        // 如果什么也没读到,则可能是因为 signal 导致的.例如 SIGINT XD
        else if(event_num == 0)
            continue;
        
        // 遍历获取到的事件
        for(int i = 0; i < event_num; i++)
        {
            // 获取事件相关的信息
            epoll_event&& event = epoll->getEvent(static_cast<size_t>(i));
            EpollEventCallback* curr_epoll_event_callback = static_cast<EpollEventCallback*>(event.data.ptr);
            (*curr_epoll_event_callback)(event);
        }
    }
    epoll->del(listen_fd);

    return 0;
}