#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <netinet/in.h>

namespace camera {

enum class SessionState {
    INIT,
    READY,
    PLAYING,
    CLOSED
};

struct RtspClientSession {
    int socket_fd{-1};
    std::string session_id;
    std::string stream_path;
    bool is_tcp_interleaved{true};
    uint8_t rtp_channel{0};
    uint8_t rtcp_channel{1};
    
    // UDP transport parameters
    struct sockaddr_in client_rtp_addr{};
    int server_rtp_fd{-1};
    uint16_t server_rtp_port{60000};
    uint16_t client_rtp_port{0};
    uint16_t client_rtcp_port{0};

    uint16_t rtp_seq{0};
    uint32_t rtp_timestamp{0};
    SessionState state{SessionState::INIT};
};

struct StreamChannel {
    std::string path;
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t fps{30};
    uint16_t rtp_seq{0};
    uint32_t rtp_timestamp{0};
    std::vector<uint8_t> sps_pps_header;
};

/**
 * @brief Ultra-lightweight, Zero-Dependency Embedded RTSP Server.
 * Implements RFC 2326 (RTSP) and RFC 6184 (RTP Payload Format for H.264).
 */
class RtspServer {
public:
    explicit RtspServer(uint16_t port = 554);
    ~RtspServer();

    RtspServer(const RtspServer&) = delete;
    RtspServer& operator=(const RtspServer&) = delete;

    /**
     * @brief Register a live streaming endpoint (e.g. "/live/main" or "/live/sub").
     */
    void add_stream(const std::string& path, uint32_t width, uint32_t height, uint32_t fps);

    /**
     * @brief Start RTSP listener thread and client servicing loop.
     */
    bool start();

    /**
     * @brief Stop RTSP server and disconnect all clients.
     */
    void stop();

    /**
     * @brief Push an encoded H.264 frame to all active clients of a stream.
     * @param path The endpoint path (e.g. "/live/main").
     * @param h264_data Raw H.264 annex-b byte stream (containing NALUs).
     * @param size Size in bytes.
     * @param is_keyframe Indicates IDR frame.
     */
    void push_frame(const std::string& path, const uint8_t* h264_data, size_t size, bool is_keyframe);

    [[nodiscard]] bool is_running() const noexcept { return is_running_; }

private:
    void listener_loop();
    void handle_client(int client_fd);
    void process_rtsp_request(int client_fd, const std::string& request);
    void send_rtp_packet(RtspClientSession& session, const uint8_t* rtp_pkt, size_t pkt_len);
    void packetize_h264_and_send(RtspClientSession& session, StreamChannel& channel,
                                 const uint8_t* nal_ptr, size_t nal_len, bool is_last_nal);

    uint16_t port_;
    int server_socket_{-1};
    std::atomic<bool> is_running_{false};
    std::thread listener_thread_;

    std::mutex channels_mutex_;
    std::unordered_map<std::string, StreamChannel> channels_;

    std::mutex sessions_mutex_;
    std::unordered_map<int, RtspClientSession> sessions_;
};

} // namespace camera
