#include "server.hpp"


int main(int argc, char* argv[]) {
    PollingServer *server_ptr = NULL;
    if (argc < 3) {
        server_ptr = new PollingServer();
        // std::cerr << "Usage: " << argv[0] << " <Server_IP> <Port>\n";
        // std::cerr << "Example: " << argv[0] << " 127.0.0.1 8080\n";
        // return 1;
    }else{
        std::string_view server_ip(argv[1]);
        int server_port = std::stoi(argv[2]);
        server_ptr = new PollingServer(server_ip, server_port);

    }


    if(server_ptr->init() != 0) return -1;
    server_ptr->set_parser_type("RESP");
    server_ptr->run();
    return 0;
}
