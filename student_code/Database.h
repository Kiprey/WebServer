#ifndef DATABASE_H
#define DATABASE_H

#include <pqxx/pqxx>
#include <queue>
#include <vector>
#include <memory> 
#include <mutex>
#include <unordered_map>
#include <functional>
#include "Log.h"
#include "Timer.h"
#include "Epoll.h"

class ConnectionPool {
public:
    /**
     * @brief 构建一个连接池
     * @param postgres_host     数据库主机地址
     * @param postgres_port     数据库端口
     * @param postgres_user     数据库用户名
     * @param postgres_password 数据库密码
     * @param postgres_dbname   数据库名
     * @param pool_size         连接池大小
     * @note 在无法连接数据库时会抛出异常
     */
    ConnectionPool(
        const std::string& postgres_host, const std::string& postgres_port,
        const std::string& postgres_user, const std::string& postgres_password,
        const std::string& postgres_dbname, size_t pool_size
    );

    /**
     * @brief 获取一个数据库连接
     * @return 数据库连接
     */
    std::unique_ptr<pqxx::connection> aquireConnection();

    /**
     * @brief 释放一个数据库连接
     * @param conn 释放的数据库连接
     * @param close 是否关闭连接
     * @note 如果 close 为 true, 则会关闭连接, 否则会放回连接池
     */
    void releaseConnection(std::unique_ptr<pqxx::connection>&& conn, bool close = false);

private:
    std::unique_ptr<pqxx::connection> createConnection();

    std::string connection_str_;
    size_t pool_size_;
    std::queue<std::unique_ptr<pqxx::connection>> connections_;
    std::mutex mutex_;
};

class DBPipeline {
public:
    using QueryCallback = std::function<void(const pqxx::result&, bool)>;

    DBPipeline(std::shared_ptr<ConnectionPool> pool, std::unique_ptr<Timer>&& timer, int poll_ms) : pool_(pool), timer_(std::move(timer)) {
        timer_->setTime(0, poll_ms * 1000);
    }

    /**
     * @brief 提交一个查询
     * @param query    查询语句
     * @param callback 查询完成后的回调函数
     */
    void submitQuery(const std::string& query, QueryCallback callback);
    /**
     * @brief 查询所有提交的查询
     * @note 会按照提交的顺序返回查询结果
     */
    void queryAll();

    int getTimerFd() { return timer_->getFd(); }
    
    void setTimerEpollEventCallback(EpollEventCallback&& cb) { timer_event_ = std::move(cb); }
    EpollEventCallback* const getTimerEpollEventCallback()  { return &timer_event_; }
    std::shared_ptr<ConnectionPool> getPool() { return pool_; }

private:
    class PipelineInFlight {
    public:
        PipelineInFlight(std::shared_ptr<ConnectionPool> pool, size_t retain_size) : 
            pool_(pool), conn_(pool_->aquireConnection()), 
            work_(std::make_unique<pqxx::work>(*conn_)), pipeline_work_(std::make_unique<pqxx::pipeline>(*work_)) {
            pipeline_work_->retain(retain_size);
        }
        ~PipelineInFlight() {
            pipeline_work_->complete();
            pool_->releaseConnection(std::move(conn_));
        }
        void submitQuery(const std::string& query, QueryCallback callback);
        bool poll();
    private:
        std::shared_ptr<ConnectionPool> pool_;
        std::unique_ptr<pqxx::connection> conn_;
        std::unique_ptr<pqxx::work> work_;
        std::unique_ptr<pqxx::pipeline> pipeline_work_;
        std::unordered_map<pqxx::pipeline::query_id, QueryCallback> queries_in_flight;
    };

    std::shared_ptr<ConnectionPool> pool_;
    std::unique_ptr<Timer> timer_;
    std::queue<std::pair<std::string, QueryCallback>> queries_;
    std::list<std::unique_ptr<PipelineInFlight>> pipelines_in_flight_;
    std::mutex mutex_;
    EpollEventCallback timer_event_;
};
#endif 
