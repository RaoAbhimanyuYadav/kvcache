#include <gtest/gtest.h>

#include "server.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

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
    std::size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t count = send(fd, request.data() + sent, request.size() - sent, MSG_NOSIGNAL);
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

bool wait_until_readable(int fd, int timeout_ms) {
    pollfd event{};
    event.fd = fd;
    event.events = POLLIN;
    return poll(&event, 1, timeout_ms) > 0 && (event.revents & (POLLIN | POLLHUP)) != 0;
}

std::string receive_exactly(int fd, std::size_t expected_size) {
    timeval timeout{};
    timeout.tv_sec = 2;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    std::string response(expected_size, '\0');
    std::size_t received = 0;
    while (received < expected_size) {
        const ssize_t count = recv(fd, response.data() + received, expected_size - received, 0);
        if (count <= 0) return {};
        received += static_cast<std::size_t>(count);
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
    const std::string error = "-ERR Unsupported Command\r\n";
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

    ASSERT_TRUE(send_all(malformed_client_fd, "?\r\n"));
    EXPECT_TRUE(receive_exactly(malformed_client_fd, 1).empty());
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
    constexpr int kRequestCount = 600000;
    std::string requests;
    requests.reserve(kRequestCount * sizeof(kPingRequest));
    for (int i = 0; i < kRequestCount; ++i) requests += kPingRequest;

    ASSERT_TRUE(send_all(client_fd, requests));
    ASSERT_TRUE(wait_until_readable(client_fd, 5000));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::string expected;
    expected.reserve(kRequestCount * (sizeof(kPongResponse) - 1));
    for (int i = 0; i < kRequestCount; ++i) expected += kPongResponse;
    EXPECT_EQ(receive_exactly(client_fd, expected.size()), expected);
    close(client_fd);
}

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
