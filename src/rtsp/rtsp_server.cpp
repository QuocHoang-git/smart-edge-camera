#include "rtsp/rtsp_server.hpp"

#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>

namespace camera {

static constexpr uint32_t RTP_CLOCK_RATE = 90000;
static constexpr size_t MAX_RTP_PAYLOAD = 1400;

RtspServer::RtspServer(uint16_t port)
    : port_(port) {
    // Ignore SIGPIPE so broken client connections do not crash the daemon
    signal(SIGPIPE, SIG_IGN);
}

RtspServer::~RtspServer() {
    stop();
}

void RtspServer::add_stream(const std::string& path, uint32_t width, uint32_t height, uint32_t fps) {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    StreamChannel ch;
    ch.path = path;
    ch.width = width;
    ch.height = height;
    ch.fps = fps;
    ch.rtp_seq = 1000;
    ch.rtp_timestamp = 0;
    channels_[path] = ch;
    std::cout << "[RTSP] Registered stream endpoint: " << path 
              << " (" << width << "x" << height << " @ " << fps << " FPS)\n";
}

bool RtspServer::start() {
    if (is_running_) return true;

    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        std::cerr << "[RTSP ERROR] Failed to create server socket!\n";
        return false;
    }

    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[RTSP ERROR] Failed to bind to port " << port_ << "!\n";
        close(server_socket_);
        server_socket_ = -1;
        return false;
    }

    if (listen(server_socket_, 10) < 0) {
        std::cerr << "[RTSP ERROR] Listen failed on port " << port_ << "!\n";
        close(server_socket_);
        server_socket_ = -1;
        return false;
    }

    is_running_ = true;
    listener_thread_ = std::thread(&RtspServer::listener_loop, this);

    std::cout << "[RTSP INFO] RTSP Server listening on rtsp://0.0.0.0:" << port_ << "/\n";
    return true;
}

void RtspServer::stop() {
    if (!is_running_) return;

    is_running_ = false;

    if (server_socket_ >= 0) {
        shutdown(server_socket_, SHUT_RDWR);
        close(server_socket_);
        server_socket_ = -1;
    }

    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }

    // Close all active sessions
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& pair : sessions_) {
        if (pair.second.server_rtp_fd >= 0) {
            close(pair.second.server_rtp_fd);
        }
        close(pair.second.socket_fd);
    }
    sessions_.clear();

    std::cout << "[RTSP INFO] RTSP Server stopped.\n";
}

void RtspServer::listener_loop() {
    while (is_running_) {
        struct pollfd pfd{};
        pfd.fd = server_socket_;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 500); // 500ms timeout
        if (ret <= 0) {
            continue;
        }

        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_socket_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
        if (client_fd < 0) {
            continue;
        }

        // Set short timeout on client socket for commands
        struct timeval tv{};
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::cout << "[RTSP INFO] Accepted connection from " << client_ip << ":" 
                  << ntohs(client_addr.sin_port) << " (fd=" << client_fd << ")\n";

        // Read initial RTSP request in separate thread
        std::thread(&RtspServer::handle_client, this, client_fd).detach();
    }
}

void RtspServer::handle_client(int client_fd) {
    std::vector<char> buffer(4096);
    while (is_running_) {
        ssize_t bytes_read = recv(client_fd, buffer.data(), buffer.size() - 1, 0);
        if (bytes_read <= 0) {
            if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // Read timeout occurred, socket is still alive, keep waiting for commands (e.g. TEARDOWN)
                continue;
            }
            // Client closed connection or unrecoverable error
            break;
        }

        buffer[bytes_read] = '\0';
        std::string request_str(buffer.data(), bytes_read);

        process_rtsp_request(client_fd, request_str);
    }

    // Mark session closed on exit
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(client_fd);
    if (it != sessions_.end()) {
        it->second.state = SessionState::CLOSED;
        if (it->second.server_rtp_fd >= 0) close(it->second.server_rtp_fd);
        close(client_fd);
        sessions_.erase(it);
    } else {
        close(client_fd);
    }
}

static std::string extract_cseq(const std::string& req) {
    size_t pos = req.find("CSeq: ");
    if (pos == std::string::npos) return "1";
    size_t end = req.find("\r\n", pos);
    return req.substr(pos + 6, end - (pos + 6));
}

static std::string extract_url(const std::string& req) {
    size_t method_end = req.find(' ');
    if (method_end == std::string::npos) return "";
    size_t url_end = req.find(' ', method_end + 1);
    if (url_end == std::string::npos) return "";
    return req.substr(method_end + 1, url_end - (method_end + 1));
}

static std::string extract_stream_path(const std::string& req) {
    std::string url = extract_url(req);
    if (url.empty()) return "/live/main";
    size_t host_pos = url.find("://");
    if (host_pos == std::string::npos) return url;
    size_t path_pos = url.find('/', host_pos + 3);
    if (path_pos == std::string::npos) return "/";

    std::string path = url.substr(path_pos);
    size_t track_pos = path.find("/track");
    if (track_pos != std::string::npos) {
        path = path.substr(0, track_pos);
    }
    return path;
}

void RtspServer::process_rtsp_request(int client_fd, const std::string& request) {
    std::string cseq = extract_cseq(request);
    std::string stream_path = extract_stream_path(request);

    std::cout << "[RTSP REQ fd=" << client_fd << "]\n" << request << "\n----------------" << std::endl;

    if (request.rfind("OPTIONS", 0) == 0) {
        std::ostringstream resp;
        resp << "RTSP/1.0 200 OK\r\n"
             << "CSeq: " << cseq << "\r\n"
             << "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY\r\n\r\n";
        std::string s = resp.str();
        send(client_fd, s.c_str(), s.length(), MSG_NOSIGNAL);
    }
    else if (request.rfind("DESCRIBE", 0) == 0) {
        std::ostringstream sdp;
        sdp << "v=0\r\n"
            << "o=- 1609459200 1 IN IP4 0.0.0.0\r\n"
            << "s=Smart Edge Camera H264\r\n"
            << "t=0 0\r\n"
            << "m=video 0 RTP/AVP 96\r\n"
            << "c=IN IP4 0.0.0.0\r\n"
            << "a=rtpmap:96 H264/90000\r\n"
            << "a=fmtp:96 packetization-mode=1;profile-level-id=42e01f\r\n"
            << "a=control:track0\r\n";
        std::string sdp_str = sdp.str();

        std::string req_url = extract_url(request);
        if (!req_url.empty() && req_url.back() != '/') {
            req_url += "/";
        }

        std::ostringstream resp;
        resp << "RTSP/1.0 200 OK\r\n"
             << "CSeq: " << cseq << "\r\n"
             << "Content-Type: application/sdp\r\n"
             << "Content-Base: " << req_url << "\r\n"
             << "Content-Length: " << sdp_str.length() << "\r\n\r\n"
             << sdp_str;
        std::string s = resp.str();
        send(client_fd, s.c_str(), s.length(), MSG_NOSIGNAL);
    }
    else if (request.rfind("SETUP", 0) == 0) {
        // Check if client requested TCP interleaved transport
        if (request.find("RTP/AVP/TCP") == std::string::npos && request.find("interleaved") == std::string::npos) {
            // Client requested UDP (RTP/AVP). Because surveillance cameras are often behind NAT/port-forwarding,
            // we reject UDP with 461 Unsupported Transport (RFC 2326 Section 12.39).
            // This instructs VLC / FFmpeg to immediately fallback to RTP/AVP/TCP (Interleaved) over port 554.
            std::cout << "[RTSP INFO] Client requested UDP transport. Rejecting with 461 to trigger TCP interleaved fallback.\n";
            std::ostringstream resp;
            resp << "RTSP/1.0 461 Unsupported Transport\r\n"
                 << "CSeq: " << cseq << "\r\n"
                 << "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n";
            std::string s = resp.str();
            send(client_fd, s.c_str(), s.length(), MSG_NOSIGNAL);
            return;
        }

        RtspClientSession session;
        session.socket_fd = client_fd;
        session.session_id = "12345678";
        session.stream_path = stream_path;
        session.state = SessionState::READY;
        session.is_tcp_interleaved = true;
        session.rtp_channel = 0;
        session.rtcp_channel = 1;

        std::ostringstream transport_resp;
        transport_resp << "RTP/AVP/TCP;unicast;interleaved=0-1";

        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            sessions_[client_fd] = session;
        }

        std::ostringstream resp;
        resp << "RTSP/1.0 200 OK\r\n"
             << "CSeq: " << cseq << "\r\n"
             << "Transport: " << transport_resp.str() << "\r\n"
             << "Session: " << session.session_id << "\r\n\r\n";
        std::string s = resp.str();
        send(client_fd, s.c_str(), s.length(), MSG_NOSIGNAL);
    }
    else if (request.rfind("PLAY", 0) == 0) {
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(client_fd);
            if (it != sessions_.end()) {
                it->second.state = SessionState::PLAYING;
                std::cout << "[RTSP INFO] Stream " << it->second.stream_path 
                          << " started PLAY for client (fd=" << client_fd << ")\n";
            }
        }

        std::string req_url = extract_url(request);
        if (!req_url.empty() && req_url.find("/track") == std::string::npos) {
            if (req_url.back() != '/') req_url += "/";
            req_url += "track0";
        }

        std::ostringstream resp;
        resp << "RTSP/1.0 200 OK\r\n"
             << "CSeq: " << cseq << "\r\n"
             << "Range: npt=0.000-\r\n"
             << "Session: 12345678\r\n"
             << "RTP-Info: url=" << req_url << "\r\n\r\n";
        std::string s = resp.str();
        send(client_fd, s.c_str(), s.length(), MSG_NOSIGNAL);
    }
    else if (request.rfind("TEARDOWN", 0) == 0) {
        std::ostringstream resp;
        resp << "RTSP/1.0 200 OK\r\n"
             << "CSeq: " << cseq << "\r\n"
             << "Session: 12345678\r\n\r\n";
        std::string s = resp.str();
        send(client_fd, s.c_str(), s.length(), MSG_NOSIGNAL);

        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(client_fd);
        if (it != sessions_.end()) {
            it->second.state = SessionState::CLOSED;
            if (it->second.server_rtp_fd >= 0) close(it->second.server_rtp_fd);
            close(client_fd);
            sessions_.erase(it);
        }
    }
}

void RtspServer::send_rtp_packet(RtspClientSession& session, const uint8_t* rtp_pkt, size_t pkt_len) {
    if (session.is_tcp_interleaved) {
        uint8_t prefix[4];
        prefix[0] = '$';
        prefix[1] = session.rtp_channel;
        prefix[2] = static_cast<uint8_t>((pkt_len >> 8) & 0xFF);
        prefix[3] = static_cast<uint8_t>(pkt_len & 0xFF);

        struct iovec iov[2];
        iov[0].iov_base = prefix;
        iov[0].iov_len = 4;
        iov[1].iov_base = const_cast<uint8_t*>(rtp_pkt);
        iov[1].iov_len = pkt_len;

        struct msghdr msg{};
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;

        ssize_t sent = sendmsg(session.socket_fd, &msg, MSG_NOSIGNAL);
        if (sent < 0) {
            session.state = SessionState::CLOSED;
        }
    } else {
        if (session.server_rtp_fd >= 0) {
            ssize_t sent = sendto(session.server_rtp_fd, rtp_pkt, pkt_len, MSG_NOSIGNAL,
                   reinterpret_cast<struct sockaddr*>(&session.client_rtp_addr),
                   sizeof(session.client_rtp_addr));
            if (sent < 0) {
                session.state = SessionState::CLOSED;
            }
        }
    }
}

void RtspServer::packetize_h264_and_send(
    RtspClientSession& session, StreamChannel& channel,
    const uint8_t* nal_ptr, size_t nal_len, bool is_last_nal) 
{
    if (nal_len == 0 || session.state != SessionState::PLAYING) return;

    if (nal_len <= MAX_RTP_PAYLOAD) {
        // Single NAL Unit Packet
        std::vector<uint8_t> rtp_buf(12 + nal_len);
        rtp_buf[0] = 0x80; // V=2
        rtp_buf[1] = (is_last_nal ? 0x80 : 0x00) | 96;
        rtp_buf[2] = static_cast<uint8_t>((channel.rtp_seq >> 8) & 0xFF);
        rtp_buf[3] = static_cast<uint8_t>(channel.rtp_seq & 0xFF);
        rtp_buf[4] = static_cast<uint8_t>((channel.rtp_timestamp >> 24) & 0xFF);
        rtp_buf[5] = static_cast<uint8_t>((channel.rtp_timestamp >> 16) & 0xFF);
        rtp_buf[6] = static_cast<uint8_t>((channel.rtp_timestamp >> 8) & 0xFF);
        rtp_buf[7] = static_cast<uint8_t>(channel.rtp_timestamp & 0xFF);
        rtp_buf[8] = 0x12; rtp_buf[9] = 0x34; rtp_buf[10] = 0x56; rtp_buf[11] = 0x78;

        std::memcpy(rtp_buf.data() + 12, nal_ptr, nal_len);
        channel.rtp_seq++;

        send_rtp_packet(session, rtp_buf.data(), rtp_buf.size());
    } else {
        // Fragmentation Unit A (FU-A) - RFC 6184 Section 5.8
        uint8_t nal_header = nal_ptr[0];
        const uint8_t* payload = nal_ptr + 1;
        size_t payload_len = nal_len - 1;

        bool first_fragment = true;
        while (payload_len > 0) {
            size_t chunk_size = std::min(payload_len, MAX_RTP_PAYLOAD);
            bool last_fragment = (chunk_size == payload_len);

            std::vector<uint8_t> rtp_buf(12 + 2 + chunk_size);
            rtp_buf[0] = 0x80;
            rtp_buf[1] = ((last_fragment && is_last_nal) ? 0x80 : 0x00) | 96;
            rtp_buf[2] = static_cast<uint8_t>((channel.rtp_seq >> 8) & 0xFF);
            rtp_buf[3] = static_cast<uint8_t>(channel.rtp_seq & 0xFF);
            rtp_buf[4] = static_cast<uint8_t>((channel.rtp_timestamp >> 24) & 0xFF);
            rtp_buf[5] = static_cast<uint8_t>((channel.rtp_timestamp >> 16) & 0xFF);
            rtp_buf[6] = static_cast<uint8_t>((channel.rtp_timestamp >> 8) & 0xFF);
            rtp_buf[7] = static_cast<uint8_t>(channel.rtp_timestamp & 0xFF);
            rtp_buf[8] = 0x12; rtp_buf[9] = 0x34; rtp_buf[10] = 0x56; rtp_buf[11] = 0x78;

            rtp_buf[12] = (nal_header & 0x60) | 28; // FU indicator

            uint8_t fu_header = nal_header & 0x1F;
            if (first_fragment) {
                fu_header |= 0x80;
                first_fragment = false;
            } else if (last_fragment) {
                fu_header |= 0x40;
            }
            rtp_buf[13] = fu_header;

            std::memcpy(rtp_buf.data() + 14, payload, chunk_size);
            channel.rtp_seq++;

            send_rtp_packet(session, rtp_buf.data(), rtp_buf.size());

            payload += chunk_size;
            payload_len -= chunk_size;
        }
    }
}

void RtspServer::push_frame(const std::string& path, const uint8_t* h264_data, size_t size, bool is_keyframe) {
    (void)is_keyframe;
    if (!h264_data || size == 0) return;

    std::lock_guard<std::mutex> lock_ch(channels_mutex_);
    auto it = channels_.find(path);
    if (it == channels_.end()) return;
    StreamChannel& ch = it->second;

    ch.rtp_timestamp += (RTP_CLOCK_RATE / ch.fps);

    // Split Annex-B stream into individual NAL units
    std::vector<std::pair<const uint8_t*, size_t>> nals;
    size_t i = 0;
    while (i + 3 < size) {
        size_t start_code_len = 0;
        if (h264_data[i] == 0x00 && h264_data[i+1] == 0x00 && h264_data[i+2] == 0x01) {
            start_code_len = 3;
        } else if (i + 4 <= size && h264_data[i] == 0x00 && h264_data[i+1] == 0x00 && 
                   h264_data[i+2] == 0x00 && h264_data[i+3] == 0x01) {
            start_code_len = 4;
        }

        if (start_code_len > 0) {
            size_t nal_start = i + start_code_len;
            size_t next_i = nal_start;
            while (next_i + 3 < size) {
                if (h264_data[next_i] == 0x00 && h264_data[next_i+1] == 0x00 && 
                    (h264_data[next_i+2] == 0x01 || (next_i + 4 <= size && h264_data[next_i+2] == 0x00 && h264_data[next_i+3] == 0x01))) {
                    break;
                }
                next_i++;
            }
            if (next_i + 3 >= size) next_i = size;
            if (next_i > nal_start) {
                nals.emplace_back(&h264_data[nal_start], next_i - nal_start);
            }
            i = next_i;
        } else {
            i++;
        }
    }

    if (nals.empty()) return;

    std::lock_guard<std::mutex> lock_sess(sessions_mutex_);
    for (auto it_sess = sessions_.begin(); it_sess != sessions_.end(); ) {
        RtspClientSession& session = it_sess->second;
        if (session.state == SessionState::CLOSED) {
            if (session.server_rtp_fd >= 0) close(session.server_rtp_fd);
            close(session.socket_fd);
            it_sess = sessions_.erase(it_sess);
        } else {
            if (session.stream_path == path && session.state == SessionState::PLAYING) {
                for (size_t n = 0; n < nals.size(); ++n) {
                    bool is_last_nal = (n + 1 == nals.size());
                    packetize_h264_and_send(session, ch, nals[n].first, nals[n].second, is_last_nal);
                }
            }
            ++it_sess;
        }
    }
}

} // namespace camera
