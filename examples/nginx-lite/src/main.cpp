#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static void close_socket(socket_t s) { closesocket(s); }
static bool socket_ok(socket_t s) { return s != INVALID_SOCKET; }
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static void close_socket(socket_t s) { close(s); }
static bool socket_ok(socket_t s) { return s >= 0; }
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include "config.hpp"
#ifndef _WIN32
#include <signal.h>
#endif
#ifdef NGINX_LITE_TLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
static mbedtls_entropy_context tls_entropy;
static mbedtls_ctr_drbg_context tls_rng;
static mbedtls_ssl_config tls_config;
static mbedtls_x509_crt tls_cert;
static mbedtls_pk_context tls_key;
static int tls_send(void *ctx, const unsigned char *buf, size_t len) {
    socket_t fd = *static_cast<socket_t *>(ctx);
#ifdef _WIN32
    int n = send(fd, reinterpret_cast<const char *>(buf), int(len), 0);
#else
    ssize_t n = send(fd, buf, len, 0);
#endif
    return n > 0 ? int(n) : MBEDTLS_ERR_NET_SEND_FAILED;
}
static int tls_recv(void *ctx, unsigned char *buf, size_t len) {
    socket_t fd = *static_cast<socket_t *>(ctx);
#ifdef _WIN32
    int n = recv(fd, reinterpret_cast<char *>(buf), int(len), 0);
#else
    ssize_t n = recv(fd, buf, len, 0);
#endif
    return n > 0 ? int(n) : n == 0 ? MBEDTLS_ERR_SSL_CONN_EOF : MBEDTLS_ERR_NET_RECV_FAILED;
}
static int tls_init(const char *cert_path, const char *key_path) {
    mbedtls_entropy_init(&tls_entropy); mbedtls_ctr_drbg_init(&tls_rng); mbedtls_ssl_config_init(&tls_config);
    mbedtls_x509_crt_init(&tls_cert); mbedtls_pk_init(&tls_key);
    const char *pers = "nginx-lite";
    int rc = mbedtls_ctr_drbg_seed(&tls_rng, mbedtls_entropy_func, &tls_entropy, reinterpret_cast<const unsigned char *>(pers), std::strlen(pers));
    if (!rc) rc = mbedtls_x509_crt_parse_file(&tls_cert, cert_path);
    if (!rc) rc = mbedtls_pk_parse_keyfile(&tls_key, key_path, nullptr, mbedtls_ctr_drbg_random, &tls_rng);
    if (!rc) rc = mbedtls_ssl_config_defaults(&tls_config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (!rc) { mbedtls_ssl_conf_rng(&tls_config, mbedtls_ctr_drbg_random, &tls_rng); rc = mbedtls_ssl_conf_own_cert(&tls_config, &tls_cert, &tls_key); }
    if (!rc) mbedtls_ssl_conf_min_tls_version(&tls_config, MBEDTLS_SSL_VERSION_TLS1_2);
    if (rc) std::fprintf(stderr, "TLS initialization failed: -0x%04x\n", unsigned(-rc));
    return rc;
}
static int tls_read(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len) { int n; do { n = mbedtls_ssl_read(ssl, buf, len); } while (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE); return n; }
static bool tls_write_all(mbedtls_ssl_context *ssl, const char *p, size_t n) { while (n) { int sent = mbedtls_ssl_write(ssl, reinterpret_cast<const unsigned char *>(p), n); if (sent <= 0) return false; p += sent; n -= size_t(sent); } return true; }
#endif

static bool send_all(socket_t fd, const char *p, size_t n) {
    while (n) {
#ifdef _WIN32
        int sent = send(fd, p, int(n), 0);
#else
        ssize_t sent = send(fd, p, n, 0);
#endif
        if (sent <= 0) return false;
        p += sent; n -= size_t(sent);
    }
    return true;
}
static bool number(const char *s, unsigned long &v) {
    char *end = nullptr; v = std::strtoul(s, &end, 10);
    return s[0] && end && !*end && v > 0 && v <= 65535;
}
static socket_t connect_backend(const char *host, unsigned long port) {
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    addrinfo *list = nullptr; char port_text[8]; std::snprintf(port_text, sizeof(port_text), "%lu", port);
    if (getaddrinfo(host, port_text, &hints, &list) != 0) return socket_t(-1);
    socket_t fd = socket_t(-1);
    for (addrinfo *it = list; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socket_ok(fd) && connect(fd, it->ai_addr, int(it->ai_addrlen)) == 0) break;
        if (socket_ok(fd)) close_socket(fd); fd = socket_t(-1);
    }
    freeaddrinfo(list); return fd;
}
struct Route {
    bool tls;
    std::string domain;
    unsigned long listen_port;
    std::string backend;
    unsigned long backend_port;
    socket_t listener;
};
static void error_response(socket_t client,
#ifdef NGINX_LITE_TLS
                           mbedtls_ssl_context *ssl,
#endif
                           const char *status, const char *body) {
    char h[512]; int n = std::snprintf(h, sizeof(h), "HTTP/1.1 %s\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", status, std::strlen(body), body);
    if (n > 0 && size_t(n) < sizeof(h)) {
#ifdef NGINX_LITE_TLS
        if (ssl) tls_write_all(ssl, h, size_t(n));
        else send_all(client, h, size_t(n));
#else
        send_all(client, h, size_t(n));
#endif
    }
}
static void handle(socket_t client,
#ifdef NGINX_LITE_TLS
                   mbedtls_ssl_context *ssl,
#endif
                   const std::vector<Route> &routes, unsigned long listen_port) {
    std::vector<char> req; req.reserve(8192); char buf[4096];
    while (req.size() < 65536) {
#ifdef NGINX_LITE_TLS
        int n = ssl ? tls_read(ssl, reinterpret_cast<unsigned char *>(buf), sizeof(buf)) : int(recv(client, buf, sizeof(buf), 0));
#else
#ifdef _WIN32
        int n = recv(client, buf, sizeof(buf), 0);
#else
        ssize_t n = recv(client, buf, sizeof(buf), 0);
#endif
#endif
        if (n <= 0) return; req.insert(req.end(), buf, buf + n);
        if (std::search(req.begin(), req.end(), "\r\n\r\n", "\r\n\r\n" + 4) != req.end()) break;
    }
    auto end = std::search(req.begin(), req.end(), "\r\n\r\n", "\r\n\r\n" + 4);
    if (end == req.end()) { error_response(client,
#ifdef NGINX_LITE_TLS
        ssl,
#endif
        "431 Request Header Fields Too Large", "Headers too large\n"); return; }
    std::string head(req.begin(), end + 4);
    size_t line = head.find("\r\n");
    if (line == std::string::npos || head.substr(0, line).find(" HTTP/1.") == std::string::npos) { error_response(client,
#ifdef NGINX_LITE_TLS
        ssl,
#endif
        "400 Bad Request", "Bad request\n"); return; }
    const Route *matched = nullptr;
    size_t at = line + 2;
    while (at < head.size()) {
        size_t next = head.find("\r\n", at); if (next == std::string::npos || next == at) break;
        std::string h = head.substr(at, next - at); size_t colon = h.find(':');
        if (colon != std::string::npos && h.substr(0, colon) == "Host") {
            std::string value = h.substr(colon + 1); while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            size_t c = value.find(':'); if (c != std::string::npos) value.resize(c);
            for (char &ch : value) if (ch >= 'A' && ch <= 'Z') ch = char(ch + 32);
            for (const Route &route : routes)
                if (route.listen_port == listen_port && value == route.domain) { matched = &route; break; }
            break;
        }
        at = next + 2;
    }
    if (!matched) { error_response(client,
#ifdef NGINX_LITE_TLS
        ssl,
#endif
        "404 Not Found", "Unknown host\n"); return; }
    size_t content_length = 0; at = line + 2;
    while (at < head.size()) {
        size_t next = head.find("\r\n", at); if (next == std::string::npos || next == at) break;
        std::string h = head.substr(at, next - at);
        if (h.size() >= 15 && h.compare(0, 15, "Content-Length:") == 0) {
            char *e = nullptr; unsigned long long v = std::strtoull(h.c_str() + 15, &e, 10);
            if (!e || *e || v > 16 * 1024 * 1024) { error_response(client,
#ifdef NGINX_LITE_TLS
                ssl,
#endif
                "413 Payload Too Large", "Body too large\n"); return; }
            content_length = size_t(v);
        }
        at = next + 2;
    }
    if (head.find("Transfer-Encoding:") != std::string::npos || head.find("transfer-encoding:") != std::string::npos) { error_response(client,
#ifdef NGINX_LITE_TLS
        ssl,
#endif
        "501 Not Implemented", "Chunked requests are not supported\n"); return; }
    size_t total = size_t(end - req.begin()) + 4 + content_length;
    while (req.size() < total) {
#ifdef NGINX_LITE_TLS
        int n = ssl ? tls_read(ssl, reinterpret_cast<unsigned char *>(buf), sizeof(buf)) : int(recv(client, buf, sizeof(buf), 0));
#else
#ifdef _WIN32
        int n = recv(client, buf, sizeof(buf), 0);
#else
        ssize_t n = recv(client, buf, sizeof(buf), 0);
#endif
#endif
        if (n <= 0) return; req.insert(req.end(), buf, buf + n);
    }
    socket_t upstream = connect_backend(matched->backend.c_str(), matched->backend_port);
    if (!socket_ok(upstream)) { error_response(client,
#ifdef NGINX_LITE_TLS
        ssl,
#endif
        "502 Bad Gateway", "Backend unavailable\n"); return; }
    bool ok = send_all(upstream, req.data(), total);
    while (ok) {
#ifdef _WIN32
        int n = recv(upstream, buf, sizeof(buf), 0);
#else
        ssize_t n = recv(upstream, buf, sizeof(buf), 0);
#endif
        if (n <= 0) break;
#ifdef NGINX_LITE_TLS
        ok = ssl ? tls_write_all(ssl, buf, size_t(n)) : send_all(client, buf, size_t(n));
#else
        ok = send_all(client, buf, size_t(n));
#endif
    }
    close_socket(upstream);
}
static socket_t create_listener(unsigned long port) {
    socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!socket_ok(listener)) { std::perror("socket"); return socket_t(-1); }
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(uint16_t(port));
    if (bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(listener, 32) != 0) {
        std::perror("bind/listen"); close_socket(listener); return socket_t(-1);
    }
#ifndef _WIN32
    if (listener >= FD_SETSIZE) { std::fprintf(stderr, "Listener exceeds FD_SETSIZE\n"); close_socket(listener); return socket_t(-1); }
#endif
    return listener;
}

int main(int argc, char **argv) {
    std::vector<std::string> config_arguments;
    std::vector<char *> config_argv;
    unsigned long legacy_listen = 0, legacy_backend = 0;
    bool legacy = (argc == 5 || argc == 7) && argv[1][0] != '-' &&
                  number(argv[2], legacy_listen) && number(argv[4], legacy_backend);
    if (argc > 1 && std::strcmp(argv[1], "--http") && std::strcmp(argv[1], "--https") && !legacy) {
        try {
            config_arguments.push_back(argv[0]);
            for (int i = 1; i < argc; ++i) {
                if (!std::strcmp(argv[i], "-c")) {
                    if (++i == argc) throw std::runtime_error("-c requires a file or directory");
                } else if (argv[i][0] == '-') throw std::runtime_error(std::string("unknown option: ") + argv[i]);
                configuration::load_path(argv[i], config_arguments);
            }
            for (std::string &arg : config_arguments) config_argv.push_back(arg.data());
            argc = int(config_argv.size()); argv = config_argv.data();
        } catch (const std::exception &e) {
            std::fprintf(stderr, "Configuration error: %s\n", e.what()); return 2;
        }
    }
    std::vector<Route> routes;
    const char *cert = nullptr, *key = nullptr;
    // Preserve the original single-listener command line.
    if ((argc == 5 || argc == 7) && argv[1][0] != '-') {
        Route route{}; route.tls = argc == 7; route.domain = argv[1]; route.backend = argv[3];
#ifndef NGINX_LITE_TLS
        if (route.tls) { std::fprintf(stderr, "HTTPS support is disabled in this build\n"); return 2; }
#endif
        if (route.domain.empty() || !number(argv[2], route.listen_port) || !number(argv[4], route.backend_port)) {
            std::fprintf(stderr, "Invalid domain or port\n"); return 2;
        }
        for (char &c : route.domain) if (c >= 'A' && c <= 'Z') c = char(c + 32);
        if (route.tls) { cert = argv[5]; key = argv[6]; }
        routes.push_back(route);
    }
    // Each option adds one listener. A single TLS certificate is currently supported.
    for (int i = routes.empty() ? 1 : argc; i < argc;) {
        bool tls = !std::strcmp(argv[i], "--https");
        if (std::strcmp(argv[i], "--http") && !tls) {
            std::fprintf(stderr, "Usage: nginx-lite [--http <domain> <listen-port> <backend> <backend-port>] [--https <domain> <listen-port> <backend> <backend-port> <cert.pem> <key.pem>]\n");
            return 2;
        }
        if (argc - i < (tls ? 7 : 5)) { std::fprintf(stderr, "Incomplete listener configuration\n"); return 2; }
#ifndef NGINX_LITE_TLS
        if (tls) { std::fprintf(stderr, "HTTPS support is disabled in this build\n"); return 2; }
#endif
        Route route{}; route.tls = tls; route.domain = argv[i + 1]; route.backend = argv[i + 3];
        if (route.domain.empty() || !number(argv[i + 2], route.listen_port) || !number(argv[i + 4], route.backend_port)) {
            std::fprintf(stderr, "Invalid domain or port\n"); return 2;
        }
        for (char &c : route.domain) if (c >= 'A' && c <= 'Z') c = char(c + 32);
        for (const Route &existing : routes) {
            if (existing.listen_port == route.listen_port && (existing.tls != tls || existing.domain == route.domain)) {
                std::fprintf(stderr, "Conflicting listener or duplicate domain on port %lu\n", route.listen_port); return 2;
            }
        }
        if (tls) {
            if (cert && (std::strcmp(cert, argv[i + 5]) || std::strcmp(key, argv[i + 6]))) {
                std::fprintf(stderr, "All HTTPS sites must share one certificate and key\n"); return 2;
            }
            cert = argv[i + 5]; key = argv[i + 6];
        }
        routes.push_back(route);
        i += tls ? 7 : 5;
    }
    if (routes.empty()) { std::fprintf(stderr, "Configure at least one --http or --https listener\n"); return 2; }
#ifdef NGINX_LITE_TLS
    if (cert && tls_init(cert, key) != 0) return 1;
#else
    (void)cert; (void)key;
#endif
#ifdef _WIN32
    WSADATA wsa{}; if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    if (routes.size() > FD_SETSIZE) { std::fprintf(stderr, "Too many listeners\n"); return 2; }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    for (size_t i = 0; i < routes.size(); ++i) {
        Route &route = routes[i];
        bool shared = false;
        for (size_t j = 0; j < i; ++j) if (routes[j].listen_port == route.listen_port) {
            route.listener = routes[j].listener; shared = true; break;
        }
        if (!shared) route.listener = create_listener(route.listen_port);
        if (!socket_ok(route.listener)) return 1;
        std::printf("nginx-lite: %s://%s:%lu -> %s:%lu\n", route.tls ? "https" : "http", route.domain.c_str(), route.listen_port, route.backend.c_str(), route.backend_port);
    }
    std::fflush(stdout);
    for (;;) {
        fd_set ready; FD_ZERO(&ready); socket_t highest = 0;
        for (const Route &route : routes) { FD_SET(route.listener, &ready); if (route.listener > highest) highest = route.listener; }
        if (select(int(highest + 1), &ready, nullptr, nullptr, nullptr) <= 0) continue;
        for (size_t i = 0; i < routes.size(); ++i) {
            const Route &route = routes[i];
            bool shared = false;
            for (size_t j = 0; j < i; ++j) if (routes[j].listener == route.listener) { shared = true; break; }
            if (shared) continue;
            if (!FD_ISSET(route.listener, &ready)) continue;
            sockaddr_in peer{};
#ifdef _WIN32
            int plen = sizeof(peer); socket_t client = accept(route.listener, reinterpret_cast<sockaddr *>(&peer), &plen);
#else
            socklen_t plen = sizeof(peer); socket_t client = accept(route.listener, reinterpret_cast<sockaddr *>(&peer), &plen);
#endif
            if (!socket_ok(client)) continue;
#ifdef NGINX_LITE_TLS
            if (route.tls) {
                mbedtls_ssl_context ssl; mbedtls_ssl_init(&ssl);
                if (mbedtls_ssl_setup(&ssl, &tls_config) == 0) {
                    mbedtls_ssl_set_bio(&ssl, &client, tls_send, tls_recv, nullptr);
                    if (mbedtls_ssl_handshake(&ssl) == 0) handle(client, &ssl, routes, route.listen_port);
                    mbedtls_ssl_close_notify(&ssl);
                }
                mbedtls_ssl_free(&ssl);
            } else handle(client, nullptr, routes, route.listen_port);
#else
            handle(client, routes, route.listen_port);
#endif
            close_socket(client);
        }
    }
}
