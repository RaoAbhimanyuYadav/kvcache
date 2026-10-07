#include <gtest/gtest.h>

#include "server.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#define LOG_LINE() std::cout << "Executing: " << __FILE__ << ":" << __LINE__ << std::endl;

namespace {

constexpr char kPingRequest[] = "*1\r\n$4\r\nPING\r\n";
constexpr char kPongResponse[] = "+PONG\r\n";

int reserve_ephemeral_port() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) return -1;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
        close(fd);
        return -1;
    }

    socklen_t address_size = sizeof(address);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &address_size) == -1) {
        close(fd);
        return -1;
    }

    const int port = ntohs(address.sin_port);
    close(fd);
    return port;
}

class ServerProcess {
public:
    ServerProcess() {
        port_ = reserve_ephemeral_port();
        if (port_ <= 0) return;

        pid_ = fork();
        if (pid_ == 0) {
            PollingServer server("127.0.0.1", port_);
            if (server.init() != 0) _exit(2);
            server.set_parser_type("RESP");
            server.run();
            _exit(0);
        }
    }

    ~ServerProcess() {
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            waitpid(pid_, nullptr, 0);
        }
    }

    ServerProcess(const ServerProcess&) = delete;
    ServerProcess& operator=(const ServerProcess&) = delete;

    int connect_client(int receive_buffer_size = 0) const {
        if (pid_ <= 0 || port_ <= 0) return -1;

        for (int attempt = 0; attempt < 100; ++attempt) {
            const int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd == -1) return -1;

            if (receive_buffer_size > 0 &&
                setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size,
                           sizeof(receive_buffer_size)) == -1) {
                close(fd);
                return -1;
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<std::uint16_t>(port_));
            inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

            if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                return fd;
            }

            close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return -1;
    }

private:
    int port_ = -1;
    pid_t pid_ = -1;
};

bool send_all(int fd, const std::string& request) {
    constexpr auto kSendTimeout = std::chrono::seconds(30);
    const auto deadline = std::chrono::steady_clock::now() + kSendTimeout;
    std::size_t sent = 0;
    while (sent < request.size()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            std::cerr << "send_all timed out after " << sent << " of " << request.size()
                      << " bytes\n";
            return false;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        pollfd event{};
        event.fd = fd;
        event.events = POLLOUT;
        const int ready = poll(&event, 1, std::max(1, static_cast<int>(remaining.count())));
        if (ready == -1 && errno == EINTR) continue;
        if (ready == 0) continue;
        if (ready == -1) {
            std::cerr << "send_all poll failed: " << strerror(errno) << '\n';
            return false;
        }

        const ssize_t count = send(fd, request.data() + sent, request.size() - sent,
                                   MSG_NOSIGNAL | MSG_DONTWAIT);
        if (count == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) {
            if (count == 0) {
                std::cerr << "send_all: socket made no progress after " << sent << " bytes\n";
                return false;
            }
            std::cerr << "send_all failed after " << sent << " of " << request.size()
                      << " bytes: " << strerror(errno) << '\n';
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

bool wait_until_readable(int fd, int timeout_ms) {
    pollfd event{};
    event.fd = fd;
    event.events = POLLIN;

    int result;
    do {
        result = poll(&event, 1, timeout_ms);
    } while (result == -1 && errno == EINTR);
    
    // 1. Check if poll failed (result == -1) or timed out (result == 0)
    if (result <= 0) {
        return false; 
    }

    // 2. poll returned > 0, meaning an event occurred. 
    // We check for readability (POLLIN), hang up (POLLHUP), or errors (POLLERR / POLLNVAL).
    // If there is an error, we return true so the caller attempts a read/write and catches it.
    return (event.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0;
}

std::string receive_exactly(int fd, std::size_t expected_size) {
    constexpr auto kReceiveTimeout = std::chrono::seconds(30);
    const auto deadline = std::chrono::steady_clock::now() + kReceiveTimeout;
    std::string response;
    response.reserve(expected_size);

    while (response.size() < expected_size) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            std::cerr << "receive timeout: " << response.size()
                    << " / " << expected_size << " bytes\n";
            break;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        pollfd event{};
        event.fd = fd;
        event.events = POLLIN;

        const int ready = poll(&event, 1, static_cast<int>(remaining.count()));
        if (ready == -1 && errno == EINTR) continue;
        if (ready <= 0 || (event.revents & POLLNVAL) != 0) break;
        if ((event.revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;

        char buffer[8192];
        const std::size_t wanted = std::min(sizeof(buffer), expected_size - response.size());
        const ssize_t count = recv(fd, buffer, wanted, MSG_DONTWAIT);
        if (count == -1 &&
            (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        if (count <= 0) break;
        response.append(buffer, static_cast<std::size_t>(count));
    }
    return response;
}

}  // namespace

TEST(PollingServerTest, InitializesOnLoopbackWithEphemeralPort) {
    PollingServer server("127.0.0.1", 0);
    EXPECT_EQ(server.init(), 0);
}

TEST(PollingServerTest, AcceptsRespParserType) {
    PollingServer server;
    EXPECT_TRUE(server.set_parser_type("RESP"));
}

TEST(PollingServerTest, RejectsUnsupportedParserType) {
    PollingServer server;
    EXPECT_FALSE(server.set_parser_type("JSON"));
}

TEST(PollingServerIntegrationTest, RespondsToPingOverTcp) {
    ServerProcess server;
    const int client_fd = server.connect_client();
    ASSERT_GE(client_fd, 0);

    ASSERT_TRUE(send_all(client_fd, kPingRequest));
    EXPECT_EQ(receive_exactly(client_fd, sizeof(kPongResponse) - 1), kPongResponse);
    close(client_fd);
}

TEST(PollingServerIntegrationTest, ProcessesPipelinedPingRequestsInOrder) {
    ServerProcess server;
    const int client_fd = server.connect_client();
    ASSERT_GE(client_fd, 0);

    const std::string requests = std::string(kPingRequest) + kPingRequest;
    ASSERT_TRUE(send_all(client_fd, requests));
    EXPECT_EQ(receive_exactly(client_fd, 2 * (sizeof(kPongResponse) - 1)),
              std::string(kPongResponse) + kPongResponse);
    close(client_fd);
}

TEST(PollingServerIntegrationTest, CompletesARequestReceivedInFragments) {
    ServerProcess server;
    const int client_fd = server.connect_client();
    ASSERT_GE(client_fd, 0);

    ASSERT_TRUE(send_all(client_fd, "*1\r\n$4\r\nPI"));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_TRUE(send_all(client_fd, "NG\r\n"));
    EXPECT_EQ(receive_exactly(client_fd, sizeof(kPongResponse) - 1), kPongResponse);
    close(client_fd);
}

TEST(PollingServerIntegrationTest, UnsupportedCommandReturnsErrorAndKeepsConnectionOpen) {
    ServerProcess server;
    const int client_fd = server.connect_client();
    ASSERT_GE(client_fd, 0);

    const std::string request = "*1\r\n$4\r\nNOPE\r\n";
    const std::string error = "-ERR Unsupported Command \'NOPE\'.\r\n";
    ASSERT_TRUE(send_all(client_fd, request));
    EXPECT_EQ(receive_exactly(client_fd, error.size()), error);

    ASSERT_TRUE(send_all(client_fd, kPingRequest));
    EXPECT_EQ(receive_exactly(client_fd, sizeof(kPongResponse) - 1), kPongResponse);
    close(client_fd);
}

TEST(PollingServerIntegrationTest, MalformedRequestClosesOnlyItsClientConnection) {
    ServerProcess server;
    const int malformed_client_fd = server.connect_client();
    ASSERT_GE(malformed_client_fd, 0);
    
    const std::string error = "-ERR";
    ASSERT_TRUE(send_all(malformed_client_fd, "?\r\n"));
    EXPECT_EQ(receive_exactly(malformed_client_fd, 1024).substr(0,4), error);
    close(malformed_client_fd);

    const int healthy_client_fd = server.connect_client();
    ASSERT_GE(healthy_client_fd, 0);
    ASSERT_TRUE(send_all(healthy_client_fd, kPingRequest));
    EXPECT_EQ(receive_exactly(healthy_client_fd, sizeof(kPongResponse) - 1), kPongResponse);
    close(healthy_client_fd);
}

TEST(PollingServerIntegrationTest, NormalClientDisconnectDoesNotStopTheServer) {
    ServerProcess server;
    const int first_client_fd = server.connect_client();
    ASSERT_GE(first_client_fd, 0);
    close(first_client_fd);

    const int second_client_fd = server.connect_client();
    ASSERT_GE(second_client_fd, 0);
    ASSERT_TRUE(send_all(second_client_fd, kPingRequest));
    EXPECT_EQ(receive_exactly(second_client_fd, sizeof(kPongResponse) - 1), kPongResponse);
    close(second_client_fd);
}

TEST(PollingServerIntegrationTest, ServesTwoClientsIndependently) {
    ServerProcess server;
    const int first_client_fd = server.connect_client();
    ASSERT_GE(first_client_fd, 0);
    const int second_client_fd = server.connect_client();
    ASSERT_GE(second_client_fd, 0);

    ASSERT_TRUE(send_all(first_client_fd, kPingRequest));
    ASSERT_TRUE(send_all(second_client_fd, kPingRequest));
    EXPECT_EQ(receive_exactly(first_client_fd, sizeof(kPongResponse) - 1), kPongResponse);
    EXPECT_EQ(receive_exactly(second_client_fd, sizeof(kPongResponse) - 1), kPongResponse);
    close(first_client_fd);
    close(second_client_fd);
}

TEST(PollingServerIntegrationTest, FlushesPendingOutputAfterClientStartsReading) {
    ServerProcess server;
    const int client_fd = server.connect_client(1024);
    ASSERT_GE(client_fd, 0);
    
    // Produce more output than a typical TCP send buffer can hold so the
    // server must retain pending bytes and resume after the client reads.
    constexpr int kRequestCount = 60000;
    std::string requests;
    requests.reserve(kRequestCount * sizeof(kPingRequest));
    
    for (int i = 0; i < kRequestCount; ++i) requests += kPingRequest;
    // std::cerr<<"Request sen start"<<std::endl;
    ASSERT_TRUE(send_all(client_fd, requests));
    std::cerr<<"Request sen end\n";
    ASSERT_TRUE(wait_until_readable(client_fd, 5000));
    std::cerr<<"WAITIFG OVER\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cerr<<"SLEEP OVER\n";
    std::string expected;
    expected.reserve(kRequestCount * (sizeof(kPongResponse) - 1));
    for (int i = 0; i < kRequestCount; ++i) expected += kPongResponse;
    std::cerr<<"RECEIVE STARTED\n";
    EXPECT_EQ(receive_exactly(client_fd, expected.size()), expected);
    std::cerr<<"RECIVE OVER\n";
    close(client_fd);
}

/* Since Program is running on single core it process request of slow client and until it process it fast client just failed*/
/*
READING on Fd: 5 started
READING on Fd: 5 ended
READING on Fd: 5 started parsing
READING on Fd: 5 ended parsing
Writing on Fd: 5 started and from read: 1
Write error on fd 5
CONNECTION CLOSED fd:5
EPOLL WAIT STARTED
EPOLL WAIT ENDED
New client connected on fd: 5
EPOLL WAIT STARTED
EPOLL WAIT ENDED
READING on Fd: 5 started
Client fd 5 disconnected.
READING on Fd: 5 ended
CONNECTION CLOSED fd:5
EPOLL WAIT STARTED
*/
TEST(PollingServerIntegrationTest, SlowReaderDoesNotBlockAnotherClient) {
    ServerProcess server;
    const int slow_client_fd = server.connect_client(1024);
    ASSERT_GE(slow_client_fd, 0);

    constexpr int kRequestCount = 600000;
    std::string requests;
    requests.reserve(kRequestCount * sizeof(kPingRequest));
    for (int i = 0; i < kRequestCount; ++i) requests += kPingRequest;

    ASSERT_TRUE(send_all(slow_client_fd, requests));
    ASSERT_TRUE(wait_until_readable(slow_client_fd, 5000));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const int fast_client_fd = server.connect_client();
    ASSERT_GE(fast_client_fd, 0);
    ASSERT_TRUE(send_all(fast_client_fd, kPingRequest));
    EXPECT_EQ(receive_exactly(fast_client_fd, sizeof(kPongResponse) - 1), kPongResponse);
    close(fast_client_fd);
    close(slow_client_fd);
}
