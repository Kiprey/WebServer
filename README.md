# WebServer

## 一、编译运行

### 方法一：一键 Docker 环境

```bash
docker-compose up
# 之后访问 localhost:8888
```

### 方法二：手动构建运行

- 使用以下指令编译:

  ```bash
  sudo apt-get install libpq-dev rapidjson-dev
  mkdir build && cd build
  cmake .. && make
  cd ../bin
  ```

- 使用以下指令运行

  ```bash
  export DATABASE_INFO="dbname=mydatabase user=myuser password=mypassword hostaddr=172.18.0.2 port=5432"
  export THREAD_POOL_SIZE="2"
  ./server <port> [<www_dir>]
  ```

## 三、测试方式

- 单个测试

  ```bash
  # 无效 HTTP 请求，执行 telnet 后随意输入并回车
  telnet localhost 8888

  # GET 请求
  curl http://localhost:8888/index.html
  curl http://localhost:8888/ping

  # POST 请求
  ## 找不到路由
  curl -X POST http://localhost:8888/api/bind1 \
      -H "Content-Type: application/json" \
      -d '{"deviceid": "f60da85d-26b2-402b-84f4-b35ee14752f3"}'
  ## JSON 无法解析
  curl -X POST http://localhost:8888/api/bind \
    -H "Content-Type: application/json" \
    -d '{1"deviceid": "f60da85d-26b2-402b-84f4-b35ee14752f3"}'
  ## 错误的 JSON 对象
  curl -X POST http://localhost:8888/api/bind \
    -H "Content-Type: application/json" \
    -d '["deviceid", "f60da85d-26b2-402b-84f4-b35ee14752f3"]'
  ## 找不到目标字段
  curl -X POST http://localhost:8888/api/bind \
    -H "Content-Type: application/json" \
    -d '{"device1id": "f60da85d-26b2-402b-84f4-b35ee14752f3"}'
  ## 目标字段非字符串类型
  curl -X POST http://localhost:8888/api/bind \
    -H "Content-Type: application/json" \
    -d '{"deviceid": 123}'
  ## deviceid 长度不对
  curl -X POST http://localhost:8888/api/bind \
    -H "Content-Type: application/json" \
    -d '{"deviceid": "123"}'
  curl -X POST http://localhost:8888/api/bind \
    -H "Content-Type: application/json" \
    -d '{"deviceid": ""}'
  ## 正确
  curl -X POST http://localhost:8888/api/bind \
    -H "Content-Type: application/json" \
    -d '{"deviceid": "f60da85d-26b2-402b-84f4-b35ee14752f3"}'

  ## JSON 无法解析
  curl -X POST http://localhost:8888/api/upload \
    -H "Content-Type: application/json" \
    -d '{1"userid": 1, "data": "exp:100;gold:100"}'
  ## 错误的 JSON 对象
  curl -X POST http://localhost:8888/api/upload \
    -H "Content-Type: application/json" \
    -d '["userid", 1, "data", "exp:100;gold:100"]'
  ## 找不到目标字段
  curl -X POST http://localhost:8888/api/upload \
    -H "Content-Type: application/json" \
    -d '{"userid1": 1, "data": "exp:100;gold:100"}'
  curl -X POST http://localhost:8888/api/upload \
    -H "Content-Type: application/json" \
    -d '{"userid": 1, "dat1a": "exp:100;gold:100"}'
  ## 目标字段非正确类型
  curl -X POST http://localhost:8888/api/upload \
    -H "Content-Type: application/json" \
    -d '{"userid": "1", "data": "exp:100;gold:100"}'
  curl -X POST http://localhost:8888/api/upload \
    -H "Content-Type: application/json" \
    -d '{"userid": 1, "data": 123}'
  ## data 长度不对
  curl -X POST http://localhost:8888/api/upload \
    -H "Content-Type: application/json" \
    -d '{"userid": 1, "data": ""}'
  curl -X POST http://localhost:8888/api/upload \
    -H "Content-Type: application/json" \
    -d '{"userid": 1, "data": "12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567"}'
  ## 正确
  curl -X POST http://localhost:8888/api/upload \
    -H "Content-Type: application/json" \
    -d '{"userid": 1, "data": "exp:100;gold:100"}'
  ```

- 使用 apache 测试工具 `ab` 来进行大批量测试

  ```bash
  # -c 并发数
  # -n 总请求数
  # -s 单个请求的超时时间

  # GET 测试 - 两线程 WebServer 测试效果：Time per request: 5.395ms (mean) / 90% 6ms
  ab -c 64 -n 1000 -s 300 http://localhost:8888/ping

  # 两线程 WebServer 测试效果：Time per request: 469ms (mean) / 90% 502ms
  echo '{"deviceid": "f60da85d-26b2-402b-84f4-b35ee14752f3"}' > /tmp/data1.json
  ab -c 64 -n 1000 -p /tmp/data1.json -T "application/json" http://localhost:8888/api/bind

  # 两线程 WebServer 测试效果：Time per request: 441ms (mean) / 90% 469ms
  echo '{"userid": 1, "data": "exp:100;gold:100"}' > /tmp/data2.json
  ab -c 64 -n 1000 -p /tmp/data2.json -T "application/json" http://localhost:8888/api/upload
  ```