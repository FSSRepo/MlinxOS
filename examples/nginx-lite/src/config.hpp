#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace configuration {
namespace fs = std::filesystem;

inline std::vector<std::string> tokenize(const std::string &text) {
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isspace(c)) { ++i; continue; }
        if (c == '#') { while (i < text.size() && text[i] != '\n') ++i; continue; }
        if (c == '{' || c == '}' || c == ';') { out.emplace_back(1, char(c)); ++i; continue; }
        std::string word;
        if (c == '\'' || c == '"') {
            char quote = text[i++]; bool closed = false;
            while (i < text.size()) {
                char ch = text[i++];
                if (ch == quote) { closed = true; break; }
                if (ch == '\\' && i < text.size()) ch = text[i++];
                word += ch;
            }
            if (!closed) throw std::runtime_error("unterminated quoted value");
        } else {
            while (i < text.size()) {
                c = static_cast<unsigned char>(text[i]);
                if (std::isspace(c) || c == '{' || c == '}' || c == ';' || c == '#') break;
                word += text[i++];
            }
        }
        if (word.empty()) throw std::runtime_error("empty configuration value");
        out.push_back(word);
    }
    return out;
}

class Parser {
    std::vector<std::string> tokens;
    size_t pos = 0;
    fs::path directory;
    std::vector<std::string> &arguments;

    std::string take() {
        if (pos == tokens.size()) throw std::runtime_error("unexpected end of configuration");
        return tokens[pos++];
    }
    void expect(const char *value) {
        if (take() != value) throw std::runtime_error(std::string("expected '") + value + "'");
    }
    std::vector<std::string> values() {
        std::vector<std::string> result;
        while (pos < tokens.size() && tokens[pos] != ";") {
            if (tokens[pos] == "{" || tokens[pos] == "}") throw std::runtime_error("missing ';'");
            result.push_back(take());
        }
        expect(";");
        if (result.empty()) throw std::runtime_error("missing directive value");
        return result;
    }
    static std::string one(const std::vector<std::string> &v, const char *directive) {
        if (v.size() != 1) throw std::runtime_error(std::string(directive) + " requires one value");
        return v[0];
    }
    std::string file_path(const std::string &value) const {
        fs::path path(value);
        return (path.is_absolute() ? path : directory / path).lexically_normal().string();
    }
    void server() {
        expect("{");
        std::string port, backend, backend_port, cert, key;
        bool tls = false, have_location = false;
        std::vector<std::string> names;
        while (pos < tokens.size() && tokens[pos] != "}") {
            std::string directive = take();
            if (directive == "location") {
                if (have_location || take() != "/") throw std::runtime_error("only one location / is supported per server");
                have_location = true; expect("{");
                if (take() != "proxy_pass") throw std::runtime_error("location supports only proxy_pass");
                std::string url = one(values(), "proxy_pass");
                expect("}");
                const std::string prefix = "http://";
                if (url.compare(0, prefix.size(), prefix)) throw std::runtime_error("proxy_pass requires an http:// backend");
                std::string address = url.substr(prefix.size());
                if (!address.empty() && address.back() == '/') address.pop_back();
                size_t colon = address.find(':');
                if (colon == std::string::npos || colon == 0 || colon + 1 == address.size() || address.find_first_of("/:?#", colon + 1) != std::string::npos)
                    throw std::runtime_error("proxy_pass requires http://host:port without a path");
                backend = address.substr(0, colon); backend_port = address.substr(colon + 1);
            } else if (directive == "listen") {
                if (!port.empty()) throw std::runtime_error("only one listen directive per server is supported");
                auto v = values();
                if (v.size() > 2 || (v.size() == 2 && v[1] != "ssl")) throw std::runtime_error("listen accepts a port and optional ssl");
                port = v[0]; tls = v.size() == 2;
            } else if (directive == "server_name") {
                if (!names.empty()) throw std::runtime_error("duplicate server_name");
                names = values();
                for (const auto &name : names) if (name.find('*') != std::string::npos) throw std::runtime_error("wildcard server names are not supported");
            } else if (directive == "ssl_certificate") {
                if (!cert.empty()) throw std::runtime_error("duplicate ssl_certificate");
                cert = file_path(one(values(), "ssl_certificate"));
            } else if (directive == "ssl_certificate_key") {
                if (!key.empty()) throw std::runtime_error("duplicate ssl_certificate_key");
                key = file_path(one(values(), "ssl_certificate_key"));
            } else throw std::runtime_error("unsupported server directive: " + directive);
        }
        expect("}");
        if (port.empty() || names.empty() || !have_location || backend.empty() || (tls && (cert.empty() || key.empty())) || (!tls && (!cert.empty() || !key.empty())))
            throw std::runtime_error("server requires listen, server_name, location / and, for ssl, certificate and key");
        for (const auto &name : names) {
            arguments.push_back(tls ? "--https" : "--http"); arguments.push_back(name);
            arguments.push_back(port); arguments.push_back(backend); arguments.push_back(backend_port);
            if (tls) { arguments.push_back(cert); arguments.push_back(key); }
        }
    }
    void context(bool http) {
        while (pos < tokens.size() && (!http || tokens[pos] != "}")) {
            std::string directive = take();
            if (directive == "server") server();
            else if (directive == "http" && !http) { expect("{"); context(true); expect("}"); }
            else throw std::runtime_error("unsupported directive: " + directive);
        }
    }
public:
    Parser(std::vector<std::string> t, fs::path dir, std::vector<std::string> &out)
        : tokens(std::move(t)), directory(std::move(dir)), arguments(out) {}
    void parse() { context(false); }
};

inline void load_file(const fs::path &file, std::vector<std::string> &out) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open " + file.string());
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    try { Parser(tokenize(text), fs::absolute(file).parent_path(), out).parse(); }
    catch (const std::exception &e) { throw std::runtime_error(file.string() + ": " + e.what()); }
}

inline void load_path(const fs::path &path, std::vector<std::string> &out) {
    if (fs::is_regular_file(path)) { load_file(path, out); return; }
    if (!fs::is_directory(path)) throw std::runtime_error("configuration path not found: " + path.string());
    std::vector<fs::path> files;
    for (const auto &entry : fs::directory_iterator(path))
        if (entry.is_regular_file() && entry.path().extension() == ".conf") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) throw std::runtime_error("no .conf files in " + path.string());
    for (const auto &file : files) load_file(file, out);
}
}
