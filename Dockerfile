FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y build-essential libpq-dev rapidjson-dev

WORKDIR /app
COPY . /app
RUN make
CMD ["./WebServer", "80"]
