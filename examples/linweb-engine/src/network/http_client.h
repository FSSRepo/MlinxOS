#pragma once
#include <map>
#include <memory>
#include <string>

namespace linweb {

struct HttpResponse {
    std::string url; // Final URL, after redirects.
    int status = 0;
    std::map<std::string, std::string> headers;
    std::string body; // Binary-safe.
};

bool is_http_url(const std::string& url);
std::string resolve_url(const std::string& base, const std::string& reference);

// Synchronous HTTP/1.1 downloader. Throws std::runtime_error on failure.
// Uses mbedTLS's portable socket backend (Winsock on Windows).
class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    // `headers` are added to the request (a Range for example); a 206 Partial Content answer is a success.
    HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace linweb
