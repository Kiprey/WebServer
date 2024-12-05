#include <vector>
#include "Database.h"
#include "Utils.h"

ConnectionPool::ConnectionPool(
    const string& postgres_host, const string& postgres_port,
    const string& postgres_user, const string& postgres_password,
    const string& postgres_dbname, size_t pool_size) : pool_size_(pool_size) {

    vector<string> resolved_ips = resolveHostnameToIPv4(postgres_host);
    
    bool is_success = false;
    for (const string& resolved_ip : resolved_ips) {
        // 设置单次超时时间为3s
        connection_str_ =   "dbname=" + postgres_dbname + \
                            " user=" + postgres_user + \
                            " password=" + postgres_password + \
                            " host=" + resolved_ip + \
                            " port=" + postgres_port + \
                            " connect_timeout=3";
        // 首先尝试连接一下看看哪个才是真的能连上的
        try {
            if (createConnection()) {
                INFO("Connected to database %s:%s", resolved_ip.c_str(), postgres_port.c_str());
                is_success = true;
                break;
            }
            // 无需处理这个连接，因为它会自动释放
        } catch (const std::exception& e) {
            ERROR("Error connecting to database %s:%s: %s", resolved_ip.c_str(), postgres_port.c_str(), e.what());
        }
    }
    if (!is_success) {
        ERROR("Failed to connect to database");
        throw std::runtime_error("Failed to connect to database");
    }
    for (size_t i = 0; i < pool_size; i++) {
        auto conn = createConnection();
        if (conn) {
            connections_.push(std::move(conn));
        } else {
            ERROR("Failed to create connection %zu", i);
        }
    }
}
    
std::unique_ptr<pqxx::connection> ConnectionPool::aquireConnection() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connections_.empty()) {
        WARN("Connection pool exhausted, creating new connection.");
        auto conn = createConnection();
        return conn;
    } else {
        auto conn = std::move(connections_.front());
        connections_.pop();
        return conn;
    }
}

void ConnectionPool::releaseConnection(std::unique_ptr<pqxx::connection>&& conn, bool close) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!close && connections_.size() < pool_size_) {
        connections_.push(std::move(conn));
    } else {
        conn.reset();
    }
}

std::unique_ptr<pqxx::connection> ConnectionPool::createConnection() {
    try {
        return std::make_unique<pqxx::connection>(connection_str_);
    } catch (const std::exception& e) {
        ERROR("Error creating connection: %s", e.what());
        return nullptr;
    }
}

void DBPipeline::submitQuery(const std::string& query, QueryCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    queries_.emplace(query, callback);
}

void DBPipeline::queryAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto iter = pipelines_in_flight_.begin(); iter != pipelines_in_flight_.end();) {
        if ((*iter)->poll())
            iter = pipelines_in_flight_.erase(iter);
        else
            ++iter;
    }

    // submit queries in batch
    if (queries_.size() > 0) {
        std::unique_ptr<PipelineInFlight> pipeline_in_flight = std::make_unique<PipelineInFlight>(pool_, queries_.size());
        while (queries_.size() > 0) {
            auto& query_item = queries_.front();
            pipeline_in_flight->submitQuery(query_item.first, std::move(query_item.second));
            queries_.pop();
        }
        pipelines_in_flight_.push_back(std::move(pipeline_in_flight));
    }
}

void DBPipeline::PipelineInFlight::submitQuery(const std::string& query, QueryCallback callback) {
    try {
        pqxx::pipeline::query_id query_id = pipeline_work_->insert(query);
        queries_in_flight[query_id] = callback;
    } catch (const std::exception& e) {
        ERROR("Error executing query: %s", e.what());
    }
}

bool DBPipeline::PipelineInFlight::poll() {
    for (auto it = queries_in_flight.begin(); it != queries_in_flight.end();) {
        auto& query_id = it->first;
        auto& callback = it->second;

        try {
            pipeline_work_->resume();
            if (pipeline_work_->is_finished(query_id)) {
                callback(pipeline_work_->retrieve(query_id), true);
                it = queries_in_flight.erase(it);
            } else {
                ++it;
            }
        } catch (const std::exception& e) {
            ERROR("Error retrieving query result: %s", e.what());
            callback(pqxx::result(), false);
            it = queries_in_flight.erase(it);
        }
    }
    return queries_in_flight.empty();
}
