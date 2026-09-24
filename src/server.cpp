#include "server.hpp"


PollingServer::PollingServer():port(6379), server_ip("127.0.0.1"), events(MAX_EVENTS){}

PollingServer::PollingServer(std::string_view ip, const int port):port(port), server_ip(ip), events(MAX_EVENTS){}

PollingServer::~PollingServer(){
    close(epoll_fd);
    close(server_fd);
}

void PollingServer::set_addr_reuse_opt(){
    int opt  = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt , sizeof(opt)) < 0) {
        std::cerr << "Warning: Failed to set SO_REUSEADDR. Errno: " << errno << "\n";
    }
}

void PollingServer::accept_connections(){
    while(true){
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept4(server_fd,(struct sockaddr*) &client_addr, &client_len, SOCK_NONBLOCK);
        if(client_fd == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                // all pending connections have accepted
                break;
            }
            std::cerr << "Accept error: " << strerror(errno) << std::endl;
            break;
        }

        epoll_event ev{};
        ev.data.fd = client_fd;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
            std::cerr << "Failed to add client fd to epoll" << std::endl;
            close(client_fd);
        } else {
            std::cout << "New client connected on fd: " << client_fd << std::endl;
        }
        client_data[client_fd] = ClientContext(parser_method);
    }
}

void PollingServer::read_data(int fd){
    char raw_buffer[BUFFER_SIZE];
    bool connection_closed = false;
    ClientContext *context = &client_data[fd];
    std::string read_buf;
    while(true){
        ssize_t bytes_read = read(fd, raw_buffer, sizeof(raw_buffer)-1);
        if(bytes_read < 0){
            if(errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "Read error on fd: " << fd << std::endl;
            break;
        }

        if(bytes_read == 0){
            std::cout << "Client fd " << fd << " disconnected." << std::endl;
            connection_closed = true;
            break;
        }

        raw_buffer[bytes_read] = '\0';
        std::cout << "[Client " << fd << "] sent: " << raw_buffer;
        context->input_parser->feed(raw_buffer, bytes_read);
    }
    if(connection_closed){
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        client_data.erase(fd);
        close(fd);
    }else{
        RESPObj result;
        
        if(context->input_parser->try_parse(result)){
            std::vector<std::string> request_array = context->input_parser->get_command_array(result);
            if(request_array.size() == 0){
                std::cout<<"REQUEST:: BAD \n";
            }else{
                if(request_array[0] == "PING"){
                    std::string resp = "+PONG\r\n";
                    std::cout<<"RESPONSE:: size = "<<resp.size()<<"\n";
                    ssize_t bytes_written = write(fd, resp.data(), sizeof(resp));
                    if (bytes_written == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                        std::cerr << "Write error on fd " << fd << std::endl;
                        connection_closed = true;
                    }
                }else{
                    std::cout<<"REQUEST:: undesired request\n";
                }
            }
        }
        /* TODO WRITE LOGIC*/
        // while(read_buf.length() > 0){
        //     ssize_t bytes_written = write(fd, read_buf.data(), read_buf.size());
        //     if (bytes_written == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
        //         std::cerr << "Write error on fd " << fd << std::endl;
        //         connection_closed = true;
        //         break;
        //     }
        //     if(bytes_written > 0){
        //         read_buf.erase(0, bytes_written);
        //     }
        // }
    }
}

int PollingServer::init(){
    epoll_event ev{};
    server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd < 0) {
        std::cerr << "Socket creation failed\n";
        return -1;
    }
    set_addr_reuse_opt();

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    // address.sin_addr.s_addr = INADDR_ANY;
    if (inet_pton(AF_INET, server_ip.c_str(), &address.sin_addr) <= 0) {
        std::cerr << "Invalid address / Address not supported\n";
        goto error_state;
    }

    // int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n";
        goto error_state;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed\n";
        goto error_state;
    }
    
    epoll_fd = epoll_create1(0);
    if (server_fd < 0) {
        std::cerr << "EPoll creation failed\n";
        goto error_state;
    }

    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        std::cerr << "Epoll ctl failed for server socket: " << strerror(errno) << std::endl;
        close(epoll_fd);
        goto error_state;
    }

    return 0;
    error_state:
        close(server_fd);
        return -1;
}

void PollingServer::run(){
    while(true){
        int nfds = epoll_wait(epoll_fd, events.data(), MAX_EVENTS, -1);
        if(nfds  == -1){
            if(errno == EINTR) continue;
            std::cerr << "Epoll wait error: " << strerror(errno) << std::endl;
            break;
        }
        for(int i=0; i<nfds; i++){
            int fd = events[i].data.fd;
            if((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
                std::cerr << "Epoll error or hangup on fd " << fd << std::endl;
                client_data.erase(fd);
                close(fd); // Automatically removes from epoll
                continue;
            }
            if(fd == server_fd){
                accept_connections();
            }else if (events[i].events&EPOLLIN){
                read_data(fd);
            }
        }
    }
}

bool PollingServer::set_parser_type(std::string_view type){
    if(type != "RESP") return false;
    parser_method = type;
    return true;
}

