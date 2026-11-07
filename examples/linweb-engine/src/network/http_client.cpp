#include "network/http_client.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

extern "C" {
extern const unsigned char linweb_ca_pem[];
extern const unsigned long linweb_ca_pem_len;
}

namespace linweb {
namespace {
constexpr size_t max_body = 64 * 1024 * 1024;
constexpr size_t max_headers = 64 * 1024;
constexpr uint32_t timeout_ms = 15000;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    auto first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
void check(int rc, const char* operation) {
    if (rc >= 0) return;
    char reason[256];
    mbedtls_strerror(rc, reason, sizeof(reason));
    throw std::runtime_error(std::string(operation) + ": " + reason);
}
struct Url {
    std::string scheme, authority, host, port, target;
    std::string origin() const { return scheme + "://" + authority; }
};
Url parse_url(std::string s) {
    if (std::any_of(s.begin(), s.end(), [](unsigned char c) { return c <= 0x20 || c == 0x7f; }))
        throw std::runtime_error("URL contains whitespace/control characters; use percent encoding");
    s = s.substr(0, s.find('#'));
    size_t scheme_end = s.find("://");
    if (scheme_end == std::string::npos) throw std::runtime_error("Expected an HTTP/HTTPS URL");
    Url u;
    u.scheme = lower(s.substr(0, scheme_end));
    if (u.scheme != "http" && u.scheme != "https") throw std::runtime_error("Unsupported URL scheme");
    size_t start = scheme_end + 3, end = s.find_first_of("/?", start);
    u.authority = s.substr(start, end == std::string::npos ? end : end - start);
    if (u.authority.empty() || u.authority.find_first_of("@\\") != std::string::npos)
        throw std::runtime_error("Invalid URL authority (credentials are not supported)");
    u.port = u.scheme == "https" ? "443" : "80";
    if (u.authority.front() == '[') {
        size_t bracket = u.authority.find(']');
        if (bracket == std::string::npos) throw std::runtime_error("Invalid IPv6 URL");
        u.host = u.authority.substr(1, bracket - 1);
        if (bracket + 1 < u.authority.size()) {
            if (u.authority[bracket + 1] != ':') throw std::runtime_error("Invalid URL port");
            u.port = u.authority.substr(bracket + 2);
        }
    } else {
        size_t colon = u.authority.find(':');
        u.host = u.authority.substr(0, colon);
        if (colon != std::string::npos) u.port = u.authority.substr(colon + 1);
    }
    if (u.host.empty() || u.port.empty() || u.port.size() > 5 ||
        u.port.find_first_not_of("0123456789") != std::string::npos ||
        std::stoul(u.port) == 0 || std::stoul(u.port) > 65535)
        throw std::runtime_error("Invalid URL host or port");
    u.target = end == std::string::npos ? "/" : s.substr(end);
    if (u.target.front() == '?') u.target = "/" + u.target;
    return u;
}
std::string normalize_path(const std::string& target) {
    size_t suffix = target.find_first_of("?#");
    std::string path = target.substr(0, suffix), result;
    // RFC 3986 dot segment removal; keep empty segments (double slashes).
    while (!path.empty()) {
        if (path.compare(0, 3, "../") == 0) path.erase(0, 3);
        else if (path.compare(0, 2, "./") == 0) path.erase(0, 2);
        else if (path.compare(0, 3, "/./") == 0) path.erase(0, 2);
        else if (path == "/.") path = "/";
        else if (path.compare(0, 4, "/../") == 0 || path == "/..") {
            path = path == "/.." ? "/" : path.substr(3);
            size_t slash = result.find_last_of('/');
            result = slash == std::string::npos ? "" : result.substr(0, slash);
        } else if (path == "." || path == "..") path.clear();
        else {
            size_t slash = path.find('/', path.front() == '/' ? 1 : 0);
            result += path.substr(0, slash);
            path = slash == std::string::npos ? "" : path.substr(slash);
        }
    }
    return (result.empty() ? "/" : result) + (suffix == std::string::npos ? "" : target.substr(suffix));
}
size_t number(const std::string& text, unsigned base) {
    if (text.empty()) throw std::runtime_error("Missing HTTP body length");
    size_t value = 0;
    for (unsigned char c : text) {
        unsigned digit = c >= '0' && c <= '9' ? c - '0' :
            c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 255;
        if (digit >= base || value > (max_body - digit) / base)
            throw std::runtime_error("Invalid HTTP length or response exceeds 64 MiB");
        value = value * base + digit;
    }
    return value;
}
// Returns false until a complete chunked body and its trailers have arrived.
bool unchunk(const std::string& data, std::string& body) {
    size_t pos = 0;
    body.clear();
    while (true) {
        size_t end = data.find("\r\n", pos);
        if (end == std::string::npos) return false;
        std::string line = data.substr(pos, end - pos);
        size_t length = number(trim(line.substr(0, line.find(';'))), 16);
        pos = end + 2;
        if (length == 0) {
            if (data.compare(pos, 2, "\r\n") == 0) return true;
            return data.find("\r\n\r\n", pos) != std::string::npos;
        }
        if (length > max_body - body.size()) throw std::runtime_error("Response exceeds 64 MiB");
        if (data.size() - pos < length + 2) return false;
        body.append(data, pos, length);
        pos += length;
        if (data.compare(pos, 2, "\r\n") != 0) throw std::runtime_error("Malformed HTTP chunk");
        pos += 2;
    }
}
struct Connection {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    Connection() { mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); }
    ~Connection() { mbedtls_ssl_free(&ssl); mbedtls_net_free(&net); }
};
} // namespace

bool is_http_url(const std::string& url) {
    std::string prefix = lower(url.substr(0, 8));
    return prefix.compare(0, 7, "http://") == 0 || prefix == "https://";
}
std::string resolve_url(const std::string& base, const std::string& reference) {
    std::string ref = trim(reference);
    if (is_http_url(ref)) {
        Url u = parse_url(ref);
        // Retain a fragment for SVG references in rewritten resources.
        size_t fragment = ref.find('#');
        return u.origin() + normalize_path(u.target) + (fragment == std::string::npos ? "" : ref.substr(fragment));
    }
    Url u = parse_url(base);
    if (ref.compare(0, 2, "//") == 0) return resolve_url(base, u.scheme + ":" + ref);
    size_t colon = ref.find(':'), slash = ref.find_first_of("/?#");
    if (colon != std::string::npos && (slash == std::string::npos || colon < slash))
        throw std::runtime_error("Unsupported resource URL scheme");
    if (ref.empty()) return u.origin() + u.target;
    if (ref.front() == '#') return u.origin() + u.target + ref;
    if (ref.front() == '?') return u.origin() + u.target.substr(0, u.target.find('?')) + ref;
    std::string path = ref.front() == '/' ? ref :
        u.target.substr(0, u.target.find('?')).substr(0, u.target.substr(0, u.target.find('?')).find_last_of('/') + 1) + ref;
    return u.origin() + normalize_path(path);
}

// LINWEB_RESOLVE=name=address[,name=address] connects to that address when the URL names that host (like curl --resolve):
// a name that no DNS knows, in a test network. The certificate is still checked against the name of the URL.
std::string connect_address(const std::string& host) {
    const char* list = std::getenv("LINWEB_RESOLVE");
    if (!list) return host;
    std::stringstream entries(list);
    for (std::string entry; std::getline(entries, entry, ',');) {
        const size_t equals = entry.find('=');
        if (equals != std::string::npos && lower(trim(entry.substr(0, equals))) == lower(host)) return trim(entry.substr(equals + 1));
    }
    return host;
}

// The bundled root certificates are parsed once for the whole process (it takes seconds), and shared, read only, by every
// client: a player that streams a file opens several.
mbedtls_x509_crt& shared_roots() {
    static mbedtls_x509_crt roots;
    static const int parsed = [] {
        mbedtls_x509_crt_init(&roots);
        int result = mbedtls_x509_crt_parse(&roots, linweb_ca_pem, linweb_ca_pem_len + 1);
        // LINWEB_CA_FILE=file.pem trusts one more certificate (a self-signed server of a test network, for example)
        const char* extra = std::getenv("LINWEB_CA_FILE");
        if (result == 0 && extra && *extra) result = mbedtls_x509_crt_parse_file(&roots, extra);
        return result;
    }();
    if (parsed != 0) throw std::runtime_error("Could not load all bundled CA certificates");
    return roots;
}

struct HttpClient::Impl {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_ssl_config config;
    Impl() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&random);
        mbedtls_ssl_config_init(&config);
    }
    ~Impl() {
        mbedtls_ssl_config_free(&config);
        mbedtls_ctr_drbg_free(&random);
        mbedtls_entropy_free(&entropy);
    }
    void init() {
        if (psa_crypto_init() != PSA_SUCCESS) throw std::runtime_error("PSA crypto initialization failed");
        const unsigned char seed[] = "linweb-http";
        check(mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, seed, sizeof(seed) - 1), "Random generator");
        mbedtls_x509_crt& roots = shared_roots();
        check(mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
              MBEDTLS_SSL_PRESET_DEFAULT), "TLS configuration");
        mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&config, &roots, nullptr);
        mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);
        mbedtls_ssl_conf_read_timeout(&config, timeout_ms);
        mbedtls_ssl_conf_min_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
    }
    HttpResponse request(const std::string& url, const std::map<std::string, std::string>& extra_headers) {
        Url u = parse_url(url);
        Connection conn;
        check(mbedtls_net_connect(&conn.net, connect_address(u.host).c_str(), u.port.c_str(), MBEDTLS_NET_PROTO_TCP), "TCP connection");
        bool tls = u.scheme == "https";
        if (tls) {
            check(mbedtls_ssl_setup(&conn.ssl, &config), "TLS setup");
            check(mbedtls_ssl_set_hostname(&conn.ssl, u.host.c_str()), "TLS hostname");
            mbedtls_ssl_set_bio(&conn.ssl, &conn.net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
            int rc;
            do { rc = mbedtls_ssl_handshake(&conn.ssl); }
            while (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE);
            if (rc < 0 && mbedtls_ssl_get_verify_result(&conn.ssl)) {
                char info[1024];
                mbedtls_x509_crt_verify_info(info, sizeof(info), "", mbedtls_ssl_get_verify_result(&conn.ssl));
                throw std::runtime_error(std::string("TLS certificate: ") + info);
            }
            check(rc, "TLS handshake");
        }
        std::string request = "GET " + u.target + " HTTP/1.1\r\nHost: " + u.authority +
            "\r\nUser-Agent: Linweb/0.1\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n";
        for (const auto& header : extra_headers) {
            if (header.first.find_first_of("\r\n:") != std::string::npos || header.second.find_first_of("\r\n") != std::string::npos)
                throw std::runtime_error("Invalid request header");
            request += header.first + ": " + header.second + "\r\n";
        }
        request += "\r\n";
        size_t sent = 0;
        while (sent < request.size()) {
            auto data = reinterpret_cast<const unsigned char*>(request.data() + sent);
            int rc = tls ? mbedtls_ssl_write(&conn.ssl, data, request.size() - sent) :
                mbedtls_net_send(&conn.net, data, request.size() - sent);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            check(rc, "HTTP write");
            if (rc == 0) throw std::runtime_error("Connection closed while sending request");
            sent += rc;
        }
        HttpResponse response;
        response.url = u.origin() + u.target;
        std::string received, raw_body;
        bool have_headers = false, chunked = false, have_length = false, complete = false;
        size_t length = 0;
        unsigned interim = 0;
        while (!complete) {
            unsigned char buffer[16384];
            int rc = tls ? mbedtls_ssl_read(&conn.ssl, buffer, sizeof(buffer)) :
                mbedtls_net_recv_timeout(&conn.net, buffer, sizeof(buffer), timeout_ms);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) rc = 0;
            // A TCP EOF is accepted only when HTTP framing proves completion or
            // the server explicitly uses a close-delimited body.
            check(rc, "HTTP read");
            if (rc == 0) {
                if (!have_headers || chunked || (have_length && raw_body.size() < length))
                    throw std::runtime_error("Truncated HTTP response");
                response.body = raw_body;
                break;
            }
            if (!have_headers) {
                received.append(reinterpret_cast<char*>(buffer), rc);
                while (!have_headers) {
                    size_t end = received.find("\r\n\r\n");
                    if (end == std::string::npos) {
                        if (received.size() > max_headers) throw std::runtime_error("HTTP headers too large");
                        break;
                    }
                    if (end > max_headers) throw std::runtime_error("HTTP headers too large");
                    std::istringstream headers(received.substr(0, end));
                    std::string version, line;
                    headers >> version >> response.status;
                    if (version.compare(0, 5, "HTTP/") != 0 || response.status < 100 || response.status > 599)
                        throw std::runtime_error("Invalid HTTP status line");
                    std::getline(headers, line);
                    response.headers.clear();
                    while (std::getline(headers, line)) {
                        size_t colon = line.find(':');
                        if (colon == std::string::npos) throw std::runtime_error("Malformed HTTP header");
                        std::string key = lower(trim(line.substr(0, colon))), value = trim(line.substr(colon + 1));
                        auto existing = response.headers.find(key);
                        if (existing != response.headers.end() && key == "content-length" && existing->second != value)
                            throw std::runtime_error("Conflicting Content-Length headers");
                        response.headers[key] = value;
                    }
                    received.erase(0, end + 4);
                    if (response.status < 200) {
                        if (response.status == 101 || ++interim > 8) throw std::runtime_error("Unsupported HTTP upgrade/interim response");
                        continue;
                    }
                    have_headers = true;
                    auto encoding = response.headers.find("content-encoding");
                    if (encoding != response.headers.end() && lower(encoding->second) != "identity")
                        throw std::runtime_error("Unsupported Content-Encoding: " + encoding->second + " (requested identity)");
                    auto transfer = response.headers.find("transfer-encoding");
                    if (transfer != response.headers.end()) {
                        if (lower(transfer->second) != "chunked") throw std::runtime_error("Unsupported Transfer-Encoding");
                        chunked = true;
                    }
                    auto content_length = response.headers.find("content-length");
                    have_length = content_length != response.headers.end() && !chunked;
                    if (have_length) length = number(content_length->second, 10);
                    raw_body.swap(received);
                }
            } else raw_body.append(reinterpret_cast<char*>(buffer), rc);
            if (!have_headers) continue;
            if (raw_body.size() > max_body + max_headers) throw std::runtime_error("Response exceeds 64 MiB");
            if (response.status == 204 || response.status == 304) { response.body.clear(); complete = true; }
            else if (chunked) complete = unchunk(raw_body, response.body);
            else if (have_length && raw_body.size() >= length) { response.body = raw_body.substr(0, length); complete = true; }
            else if (!have_length && raw_body.size() > max_body) throw std::runtime_error("Response exceeds 64 MiB");
        }
        return response;
    }
};

HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) { impl_->init(); }
HttpClient::~HttpClient() = default;
HttpResponse HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers) {
    std::string current = resolve_url(url, "");
    for (int redirects = 0; redirects <= 10; ++redirects) {
        HttpResponse response = impl_->request(current, headers);
        if (response.status == 301 || response.status == 302 || response.status == 303 ||
            response.status == 307 || response.status == 308) {
            auto location = response.headers.find("location");
            if (location == response.headers.end()) throw std::runtime_error("Redirect without Location header");
            current = resolve_url(response.url, location->second);
        } else {
            if (response.status < 200 || response.status >= 300)
                throw std::runtime_error("HTTP " + std::to_string(response.status) + " for " + response.url);
            return response;
        }
    }
    throw std::runtime_error("Too many HTTP redirects (maximum 10)");
}
} // namespace linweb
