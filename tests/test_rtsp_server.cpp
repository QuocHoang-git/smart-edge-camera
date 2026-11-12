/**
 * @file test_rtsp_server.cpp
 * @brief Integration tests for embedded RTSP Server and RTP Streaming.
 */

#include "rtsp/rtsp_server.hpp"
#include "test_framework.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>

TEST_CASE(rtsp_server_lifecycle) {
    constexpr int TEST_PORT = 8554;
    camera::RtspServer server(TEST_PORT);

    server.add_stream("/live/main", 1920, 1080, 30);
    server.add_stream("/live/sub", 640, 360, 30);

    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.stop();
}

TEST_CASE(rtsp_protocol_handshake) {
    constexpr int TEST_PORT = 8555;
    camera::RtspServer server(TEST_PORT);
    server.add_stream("/live/sub", 640, 360, 30);
    ASSERT_TRUE(server.start());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Connect as client via TCP
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(sock, 0);

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TEST_PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    int conn = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    ASSERT_GE(conn, 0);

    // Send OPTIONS request
    const char* options_req = "OPTIONS rtsp://127.0.0.1:8555/live/sub RTSP/1.0\r\nCSeq: 1\r\n\r\n";
    send(sock, options_req, strlen(options_req), 0);

    char buf[1024];
    std::memset(buf, 0, sizeof(buf));
    int bytes = recv(sock, buf, sizeof(buf) - 1, 0);
    ASSERT_GE(bytes, 10);
    ASSERT_NE(std::strstr(buf, "200 OK"), nullptr);
    ASSERT_NE(std::strstr(buf, "Public:"), nullptr);

    // Send DESCRIBE request
    const char* describe_req = "DESCRIBE rtsp://127.0.0.1:8555/live/sub RTSP/1.0\r\nCSeq: 2\r\nAccept: application/sdp\r\n\r\n";
    send(sock, describe_req, strlen(describe_req), 0);

    std::memset(buf, 0, sizeof(buf));
    bytes = recv(sock, buf, sizeof(buf) - 1, 0);
    ASSERT_GE(bytes, 10);
    ASSERT_NE(std::strstr(buf, "200 OK"), nullptr);
    ASSERT_NE(std::strstr(buf, "application/sdp"), nullptr);

    close(sock);
    server.stop();
}

TEST_CASE(rtsp_push_encoded_frame) {
    constexpr int TEST_PORT = 8556;
    camera::RtspServer server(TEST_PORT);
    server.add_stream("/live/main", 1920, 1080, 30);
    ASSERT_TRUE(server.start());

    // Dummy H.264 NAL (SPS/PPS simulation)
    uint8_t dummy_nal[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f};
    server.push_frame("/live/main", dummy_nal, sizeof(dummy_nal), true);

    server.stop();
}

int main() {
    return test_runner::run_all_tests("RTSP Server & Protocol Tests");
}
