#pragma once

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <cerrno>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>

#include "parser_strategy.hpp"
#include "resp_parser.hpp"
#include "query_executer.hpp"

constexpr int MAX_EVENTS = 64;
constexpr int BUFFER_SIZE = 4096;

struct ClientContext{
    std::string output_buffer;
    std::unique_ptr<ParserStrategy> input_parser;
    bool writer_blocked;
    ClientContext():writer_blocked(false){}
    ClientContext(std::string_view parser):writer_blocked(false){
        if(parser == "RESP"){
            input_parser = std::make_unique<RESPParser>();
        }
    }
};

class PollingServer{
    int port;
    std::string server_ip;
    int server_fd;
    int epoll_fd;
    std::vector<epoll_event> events;
    std::unordered_map<int, ClientContext> client_data;
    std::string parser_method;
    std::unique_ptr<QueryExecuter> query_executer;

    void set_addr_reuse_opt();

    void accept_connections();

    void close_connection(int fd);

    void read_data(int fd);

    void write_data(int fd, bool from_read);

    public:
    
    PollingServer();
    PollingServer(std::string_view ip, const int port);
    ~PollingServer();

    int init();

    void run();
    bool set_parser_type(std::string_view type);
};



