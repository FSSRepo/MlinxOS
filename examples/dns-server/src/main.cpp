#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static void close_socket(socket_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static void close_socket(socket_t s) { close(s); }
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static uint16_t u16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }

// Decode a conventional, uncompressed question name. Compression pointers in
// the question are rejected so malformed packets cannot cause pointer loops.
static bool read_name(const uint8_t *p, size_t n, size_t &at, std::string &name) {
    name.clear();
    for (;;) {
        if (at >= n) return false;
        uint8_t len = p[at++];
        if (!len) return true;
        if ((len & 0xc0) || len > 63 || at + len > n) return false;
        if (!name.empty()) name.push_back('.');
        for (uint8_t i = 0; i < len; ++i) {
            char c = char(p[at++]);
            if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
            name.push_back(c);
        }
        if (name.size() > 253) return false;
    }
}

int main(int argc, char **argv) {
    const char *bind_ip = "127.0.0.1", *domain = "mini.local", *answer_ip = "127.0.0.1";
    unsigned long port = 5353;
    if (argc > 5) {
        std::fprintf(stderr, "Usage: steward-dns [domain] [IPv4] [port] [bind-ip]\n"); return 2;
    }
    if (argc > 1) domain = argv[1];
    if (argc > 2) answer_ip = argv[2];
    if (argc > 3) { char *end = nullptr; port = std::strtoul(argv[3], &end, 10); if (!end || *end || port < 1 || port > 65535) return 2; }
    if (argc > 4) bind_ip = argv[4];
    std::vector<std::string> zones;
    std::string zone_arg(domain);
    size_t start = 0;
    while (start <= zone_arg.size()) {
        size_t comma = zone_arg.find(',', start);
        std::string zone = zone_arg.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        while (!zone.empty() && zone.back() == '.') zone.pop_back();
        for (char &c : zone) if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
        if (!zone.empty()) zones.push_back(zone);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (zones.empty()) { std::fprintf(stderr, "At least one domain is required\n"); return 2; }
    in_addr ip{};
    if (inet_pton(AF_INET, answer_ip, &ip) != 1) { std::fprintf(stderr, "Invalid answer IPv4 address\n"); return 2; }
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::fprintf(stderr, "WSAStartup failed\n"); return 1; }
#endif
    socket_t fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (fd == INVALID_SOCKET) { std::fprintf(stderr, "socket failed: %d\n", WSAGetLastError()); WSACleanup(); return 1; }
#else
    if (fd < 0) { std::perror("socket"); return 1; }
#endif
    sockaddr_in local{}; local.sin_family = AF_INET; local.sin_port = htons(uint16_t(port));
    if (inet_pton(AF_INET, bind_ip, &local.sin_addr) != 1 || bind(fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
        std::fprintf(stderr, "Could not bind %s:%lu\n", bind_ip, port); close_socket(fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    std::printf("Steward DNS: %s -> %s, UDP %s:%lu (A records; Ctrl+C to stop)\n", zone_arg.c_str(), answer_ip, bind_ip, port);
    for (;;) {
        uint8_t request[1500], response[1500]; sockaddr_in peer{};
#ifdef _WIN32
        int peer_len = sizeof(peer); int got = recvfrom(fd, reinterpret_cast<char *>(request), sizeof(request), 0, reinterpret_cast<sockaddr *>(&peer), &peer_len);
#else
        socklen_t peer_len = sizeof(peer); ssize_t got = recvfrom(fd, request, sizeof(request), 0, reinterpret_cast<sockaddr *>(&peer), &peer_len);
#endif
        if (got < 12) continue;
        size_t pos = 12; std::string qname;
        if (!read_name(request, size_t(got), pos, qname) || pos + 4 > size_t(got)) continue;
        uint16_t qtype = u16(request + pos), qclass = u16(request + pos + 2); pos += 4;
        bool match = std::find(zones.begin(), zones.end(), qname) != zones.end() && qtype == 1 && qclass == 1 && u16(request + 4) == 1 && !(u16(request + 2) & 0x7800);
        size_t question_end = pos;
        std::memcpy(response, request, 12);
        put16(response + 2, uint16_t(0x8000 | (u16(request + 2) & 0x0110) | (match ? 0 : 3))); // QR, copy RD/CD, NXDOMAIN otherwise
        put16(response + 4, 1); put16(response + 6, match ? 1 : 0); put16(response + 8, 0); put16(response + 10, 0);
        std::memcpy(response + 12, request + 12, question_end - 12);
        size_t out = question_end;
        if (match) {
            response[out++] = 0xc0; response[out++] = 0x0c; // name pointer to question
            put16(response + out, 1); out += 2; put16(response + out, 1); out += 2;
            response[out++] = 0; response[out++] = 0; response[out++] = 0; response[out++] = 60;
            put16(response + out, 4); out += 2;
            std::memcpy(response + out, &ip.s_addr, 4); out += 4;
        }
#ifdef _WIN32
        sendto(fd, reinterpret_cast<const char *>(response), int(out), 0, reinterpret_cast<sockaddr *>(&peer), peer_len);
#else
        sendto(fd, response, out, 0, reinterpret_cast<sockaddr *>(&peer), peer_len);
#endif
    }
}
