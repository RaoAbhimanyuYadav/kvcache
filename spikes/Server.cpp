#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <cerrno>
#include <netinet/tcp.h> // Required for TCP_NODELAY

int socket_setup(const char* ip,const int port){
    // int socket(int domain, int type, int protocol);
    int device_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (device_fd < 0) {
        std::cerr << "Socket creation failed\n";
        return -1;
    }

    // int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
    int enable_flag  = 1;
    if(setsockopt(device_fd, SOL_SOCKET, SO_REUSEADDR, &enable_flag , sizeof(enable_flag)) < 0) {
        std::cerr << "Warning: Failed to set SO_REUSEADDR. Errno: " << errno << "\n";
    }


    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    // address.sin_addr.s_addr = INADDR_ANY;
    if (inet_pton(AF_INET, ip, &address.sin_addr) <= 0) {
        std::cerr << "Invalid address / Address not supported\n";
        close(device_fd);
        return -1;
    }

    // int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
    if (bind(device_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n";
        close(device_fd);
        return -1;
    }

    return device_fd;
}

void connect_to_client(int server_fd){
    sockaddr_in client_address{};
    socklen_t addr_len = sizeof(client_address);
    // int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &addr_len);
    if (client_fd < 0) {
        std::cerr << "Accept failed\n";
        close(server_fd);
        return;
    }
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_address.sin_addr), ip_str,INET_ADDRSTRLEN);
    std::cout << "Client connected! "<<ip_str<<std::endl;

    char raw_buffer[1024];
    while (true) {
        std::memset(raw_buffer, 0, sizeof(raw_buffer));
        // ssize_t recv(int sockfd, void *buf, size_t len, int flags);
        ssize_t bytes_received = recv(client_fd, raw_buffer, sizeof(raw_buffer) - 1, 0);

        if (bytes_received < 0) {
            std::cerr << "Receive error\n";
            break;
        }
        if (bytes_received == 0) {
            std::cout << "Client disconnected gracefully.\n";
            break;
        }
        std::cout << "Received: " << raw_buffer << "\n";

    }

    close(client_fd);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <Server_IP> <Port>\n";
        std::cerr << "Example: " << argv[0] << " 127.0.0.1 8080\n";
        return 1;
    }

    const char* server_ip = argv[1];
    int server_port = std::stoi(argv[2]);

    int server_fd = socket_setup(server_ip, server_port);

    if(server_fd < 0){
        std::cerr<<"Server Setup failed\n";
        return 1;
    }

    // int listen(int sockfd, int backlog);
    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed\n";
        close(server_fd);
        return 1;
    }
    std::cout << "Server listening on port "<<server_port<<"...\n";

    connect_to_client(server_fd);

    // int close(int fd);
    close(server_fd);
    return 0;
}