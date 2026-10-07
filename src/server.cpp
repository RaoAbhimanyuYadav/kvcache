#include "server.hpp"


PollingServer::PollingServer():port(6379), server_ip("127.0.0.1"), events(MAX_EVENTS){}

PollingServer::PollingServer(std::string_view ip, const int port):port(port), server_ip(ip), events(MAX_EVENTS){}

PollingServer::~PollingServer(){
    for (const auto& [fd, context] : client_data) {
        (void)context;
        close(fd);
    }
    if (epoll_fd >= 0) close(epoll_fd);
    if (server_fd >= 0) close(server_fd);
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
            client_data.emplace(client_fd, ClientContext(parser_method));
            // std::cout << "New client connected on fd: " << client_fd << std::endl;
        }
    }
}

void PollingServer::close_connection(int fd){
    // std::cerr<<"CONNECTION CLOSED fd:"<<fd<<"\n";
    const auto client = client_data.find(fd);
    if (client == client_data.end()) return;
    if (epoll_fd >= 0) epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    client_data.erase(client);
}

void PollingServer::read_data(int fd){
    char raw_buffer[BUFFER_SIZE];
    auto client = client_data.find(fd);
    if (client == client_data.end()) return;
    ClientContext *context = &client->second;
    bool peer_closed = false;
    // std::cout<<"READING on Fd: "<<fd<<" started \n";
    while(true){
        const ssize_t bytes_read = read(fd, raw_buffer, sizeof(raw_buffer));
        if (bytes_read < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "Read error on fd " << fd << ": " << strerror(errno) << std::endl;
            close_connection(fd);
            return;
        }

        if (bytes_read == 0) {
            // std::cout << "Client fd " << fd << " disconnected." << std::endl;
            peer_closed = true;
            break;
        }

        context->input_parser->feed(raw_buffer, bytes_read);
    }
    // std::cout<<"READING on Fd: "<<fd<<" ended \n";
    RESPObj result;
    // Process complete requests already read even if EOF was observed in the
    // same drain. A hangup event can be delivered together with readable data.
    ParseResult parse_status;
    while((parse_status = context->input_parser->try_parse(result)) == ParseResult::Success){
        std::vector<std::string> request_array = context->input_parser->get_command_array(result);
        context->output_buffer += query_executer->execute(request_array);
    }
    if(parse_status == ParseResult::Rejection){
        std::vector<std::string> request_array = context->input_parser->get_command_array(result);
        context->output_buffer += query_executer->execute(request_array);
        context->close_after_write = true;
    }
    if (peer_closed) context->close_after_write = true;
    write_data(fd, true);
}

void PollingServer::write_data(int fd, bool from_read){
    auto client = client_data.find(fd);
    if (client == client_data.end()) return;
    ClientContext *context = &client->second;
    if(from_read && context->writer_blocked){
        std::cout<<"WRITER BLOCKED\n";
        return;
    }
    // std::cout<<"Writing on Fd: "<<fd<<" started and from read: "<<from_read<<"\n";

    while(context->output_buffer.size() > 0){
        const ssize_t bytes_written = send(fd, context->output_buffer.data(),
                                           context->output_buffer.size(), MSG_NOSIGNAL);
        if (bytes_written == -1) {
            if(errno == EINTR){
                continue;
            }
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                // EPOLLOUT
                epoll_event ev{};
                ev.data.fd = fd;
                if(context->close_after_write){
                    ev.events = EPOLLET | EPOLLRDHUP | EPOLLOUT;
                }else{
                    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT;
                }
                // std::cerr << "[server] socket full; queued "<< context->output_buffer.size() << " response bytes\n";
                if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1){
                    std::cerr<<"EPOLL MOD TO ENABLEING EPOLLOUT FAILED\n";
                    close_connection(fd);
                    return;
                }
                
                // std::cout<<"Writing on Fd: "<<fd<<" paused\n";
                context->writer_blocked = true;
                break;
            }

            std::cerr << "Write error on fd " << fd << std::endl;
            close_connection(fd);
            return;
        }
        if(bytes_written == 0) {
            close_connection(fd);
            std::cerr<<"ZERO BYTES WRITTEN\n";
            return;
        }

        context->output_buffer.erase(0, bytes_written);
    }
    if(context->output_buffer.size() == 0) {
        epoll_event ev{};
        ev.data.fd = fd;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1){
            std::cerr<<"COMPLETE BUFFER WRITE THEN EPOLL MOD FAILED\n";
            close_connection(fd);
            return;
        }
        
        // std::cout<<"Writing on Fd: "<<fd<<" finished\n";
        context->writer_blocked = false;

        if (context->close_after_write) {
            close_connection(fd);
            return;
        }
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
    if (epoll_fd < 0) {
        std::cerr << "EPoll creation failed\n";
        goto error_state;
    }

    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        std::cerr << "Epoll ctl failed for server socket: " << strerror(errno) << std::endl;
        close(epoll_fd);
        epoll_fd = -1;
        goto error_state;
    }

    query_executer = std::make_unique<QueryExecuter>();

    return 0;
    error_state:
        if (server_fd >= 0) {
            close(server_fd);
            server_fd = -1;
        }
        return -1;
}

void PollingServer::run(){
    while(true){
        
        // std::cout<<"EPOLL WAIT STARTED \n";
        int nfds = epoll_wait(epoll_fd, events.data(), MAX_EVENTS, -1);
        if(nfds  == -1){
            if(errno == EINTR) continue;
            std::cerr << "Epoll wait error: " << strerror(errno) << std::endl;
            break;
        }
        
        // std::cout<<"EPOLL WAIT ENDED \n";
        for(int i=0; i<nfds; i++){
            int fd = events[i].data.fd;
            if (fd == server_fd) {
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    std::cerr << "Epoll error or hangup on listening socket" << std::endl;
                    continue;
                }
                accept_connections();
                continue;
            }

            if (events[i].events & EPOLLERR) {
                int socket_error = 0;
                socklen_t length = sizeof(socket_error);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &length);

                std::cerr << "EPOLLERR fd=" << fd
                        << " SO_ERROR=" << socket_error
                        << " (" << strerror(socket_error) << ")\n";
                std::cerr << "Epoll error on fd " << fd << std::endl;
                close_connection(fd);
                continue;
            }

            // HUP/RDHUP can arrive with unread bytes. Drain and parse those
            // bytes first; read_data closes only after pending responses flush.
            if (events[i].events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
                read_data(fd);
            }

            // Read and write readiness can be reported together. Process both
            // flags so an EPOLLOUT edge is not lost after handling EPOLLIN.
            if (client_data.find(fd) != client_data.end() &&
                (events[i].events & EPOLLOUT)) {
                write_data(fd);
            }

            // If HUP was reported without read_data observing EOF, finish
            // cleanup only after giving it the chance to drain the socket.
            if (client_data.find(fd) != client_data.end() &&
                (events[i].events & EPOLLHUP) &&
                !client_data.at(fd).close_after_write) {
                close_connection(fd);
            }
        }
    }
}

bool PollingServer::set_parser_type(std::string_view type){
    if(type != "RESP") return false;
    parser_method = type;
    return true;
}

