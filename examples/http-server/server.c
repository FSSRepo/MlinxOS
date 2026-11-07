#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static int send_all(int fd, const void *data, size_t length)
{
    const char *p = data;
    while (length) {
        ssize_t n = send(fd, p, length, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n;
        length -= (size_t)n;
    }
    return 0;
}

static const char *content_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (!strcmp(ext, ".html") || !strcmp(ext, ".htm")) return "text/html; charset=utf-8";
    if (!strcmp(ext, ".css")) return "text/css; charset=utf-8";
    if (!strcmp(ext, ".js")) return "application/javascript; charset=utf-8";
    if (!strcmp(ext, ".json")) return "application/json; charset=utf-8";
    if (!strcmp(ext, ".svg")) return "image/svg+xml";
    if (!strcmp(ext, ".png")) return "image/png";
    if (!strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg")) return "image/jpeg";
    if (!strcmp(ext, ".ico")) return "image/x-icon";
    if (!strcmp(ext, ".txt")) return "text/plain; charset=utf-8";
    if (!strcmp(ext, ".mp3")) return "audio/mpeg";
    if (!strcmp(ext, ".mp4") || !strcmp(ext, ".m4v")) return "video/mp4";
    if (!strcmp(ext, ".m4a")) return "audio/mp4";
    if (!strcmp(ext, ".webm")) return "video/webm";
    if (!strcmp(ext, ".ogg") || !strcmp(ext, ".oga")) return "audio/ogg";
    if (!strcmp(ext, ".wav")) return "audio/wav";
    if (!strcmp(ext, ".flac")) return "audio/flac";
    return "application/octet-stream";
}

static int hex_value(char c)
{
    return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

/* "%20" and the like back to the characters of a file name, in place; 0 when a % does not start a hex pair or the
   result would hold a NUL */
static int percent_decode(char *text)
{
    char *out = text;
    for (const char *in = text; *in; ++in) {
        if (*in == '%') {
            if (!isxdigit((unsigned char)in[1]) || !isxdigit((unsigned char)in[2])) return 0;
            int c = hex_value(in[1]) * 16 + hex_value(in[2]);
            if (c == 0) return 0;
            *out++ = (char)c;
            in += 2;
        } else {
            *out++ = *in;
        }
    }
    *out = '\0';
    return 1;
}

/* The Range header of the request ("bytes=A-B", "bytes=A-" or "bytes=-N"; one range only). 0: no usable header, the
   whole file is sent; 1: *first..*last (inclusive) is wanted; -1: it cannot be satisfied (416). */
static int parse_range(const char *request, off_t size, off_t *first, off_t *last)
{
    const char *line = strstr(request, "\r\n");
    while (line) {
        line += 2;
        if (!strncasecmp(line, "Range:", 6)) break;
        line = strstr(line, "\r\n");
    }
    if (!line) return 0;
    const char *p = line + 6;
    while (*p == ' ' || *p == '\t') ++p;
    if (strncasecmp(p, "bytes=", 6)) return 0;
    p += 6;
    char *end;
    long long a, b;
    if (*p == '-') {
        a = strtoll(p + 1, &end, 10);
        if (end == p + 1 || a <= 0) return -1;
        *first = size > a ? size - (off_t)a : 0;
        *last = size - 1;
    } else {
        a = strtoll(p, &end, 10);
        if (end == p || *end != '-') return -1;
        p = end + 1;
        if (*p == '\r' || *p == '\0' || *p == ' ') {
            b = size - 1;
        } else {
            b = strtoll(p, &end, 10);
            if (end == p || b < a) return -1;
            if (*end == ',') return 0;   /* several ranges: not supported, the whole file is sent */
        }
        if (a >= size) return -1;
        *first = (off_t)a;
        *last = (off_t)(b < size - 1 ? b : size - 1);
    }
    return size > 0 ? 1 : -1;
}

static int within_root(const char *root, const char *path)
{
    size_t n = strlen(root);
    return !strncmp(root, path, n) && (path[n] == '\0' || path[n] == '/');
}

static void reply(int fd, const char *status, const char *type, const char *body,
                  size_t length, int head)
{
    char headers[512];
    int n = snprintf(headers, sizeof(headers),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        status, type, length);
    if (n > 0 && (size_t)n < sizeof(headers) && send_all(fd, headers, (size_t)n) == 0 && !head)
        (void)send_all(fd, body, length);
}

static void serve(int fd, const char *root)
{
    char request[8192], method[16], target[4096], version[16];
    size_t used = 0;
    struct timeval timeout = {5, 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    while (used < sizeof(request) - 1) {
        ssize_t n = recv(fd, request + used, sizeof(request) - 1 - used, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        used += (size_t)n;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n")) break;
    }

    int head = 0;
    if (!strstr(request, "\r\n\r\n") ||
        sscanf(request, "%15s %4095s %15s", method, target, version) != 3 ||
        (strcmp(version, "HTTP/1.0") && strcmp(version, "HTTP/1.1"))) {
        static const char body[] = "Bad request\n";
        reply(fd, "400 Bad Request", "text/plain; charset=utf-8", body, sizeof(body)-1, 0);
        return;
    }
    head = !strcmp(method, "HEAD");
    if (strcmp(method, "GET") && !head) {
        static const char body[] = "Method not allowed\n";
        reply(fd, "405 Method Not Allowed", "text/plain; charset=utf-8", body, sizeof(body)-1, 0);
        return;
    }
    if (!strcmp(target, "/health")) {
        static const char body[] = "{\"status\":\"ok\"}\n";
        reply(fd, "200 OK", "application/json; charset=utf-8", body, sizeof(body)-1, head);
        return;
    }

    char *query = strchr(target, '?');
    if (query) *query = '\0';
    if (!percent_decode(target) || target[0] != '/' || strstr(target, "\\") || strstr(target, "/..") ||
        !strcmp(target, "/..")) {
        static const char body[] = "Not found\n";
        reply(fd, "404 Not Found", "text/plain; charset=utf-8", body, sizeof(body)-1, head);
        return;
    }

    char candidate[PATH_MAX], resolved[PATH_MAX];
    const char *relative = target + 1;
    if (!*relative) relative = "index.html";
    int n = snprintf(candidate, sizeof(candidate), "%s/%s", root, relative);
    if (n < 0 || (size_t)n >= sizeof(candidate) || !realpath(candidate, resolved) ||
        !within_root(root, resolved)) {
        static const char body[] = "Not found\n";
        reply(fd, "404 Not Found", "text/plain; charset=utf-8", body, sizeof(body)-1, head);
        return;
    }
    struct stat st;
    if (stat(resolved, &st) < 0 || !S_ISREG(st.st_mode)) {
        static const char body[] = "Not found\n";
        reply(fd, "404 Not Found", "text/plain; charset=utf-8", body, sizeof(body)-1, head);
        return;
    }
    FILE *file = fopen(resolved, "rb");
    if (!file) {
        static const char body[] = "Not found\n";
        reply(fd, "404 Not Found", "text/plain; charset=utf-8", body, sizeof(body)-1, head);
        return;
    }
    off_t first = 0, last = st.st_size - 1;
    int ranged = parse_range(request, st.st_size, &first, &last);
    char headers[640];
    if (ranged < 0) {
        n = snprintf(headers, sizeof(headers),
            "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%lld\r\nContent-Length: 0\r\n"
            "Connection: close\r\n\r\n", (long long)st.st_size);
        if (n > 0 && (size_t)n < sizeof(headers)) (void)send_all(fd, headers, (size_t)n);
        fclose(file);
        return;
    }
    off_t length = st.st_size > 0 ? last - first + 1 : 0;
    if (ranged == 1) {
        n = snprintf(headers, sizeof(headers),
            "HTTP/1.1 206 Partial Content\r\nContent-Type: %s\r\nContent-Length: %lld\r\n"
            "Content-Range: bytes %lld-%lld/%lld\r\nAccept-Ranges: bytes\r\n"
            "Connection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
            content_type(resolved), (long long)length, (long long)first, (long long)last, (long long)st.st_size);
    } else {
        n = snprintf(headers, sizeof(headers),
            "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\nAccept-Ranges: bytes\r\n"
            "Connection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
            content_type(resolved), (long long)st.st_size);
    }
    if (n > 0 && (size_t)n < sizeof(headers) && send_all(fd, headers, (size_t)n) == 0 && !head &&
        fseeko(file, first, SEEK_SET) == 0) {
        char buffer[8192];
        while (length > 0) {
            size_t want = length < (off_t)sizeof(buffer) ? (size_t)length : sizeof(buffer);
            size_t count = fread(buffer, 1, want, file);
            if (count == 0 || send_all(fd, buffer, count) < 0) break;
            length -= (off_t)count;
        }
    }
    fclose(file);
}

int main(int argc, char **argv)
{
    long port = 8080;
    const char *document_root = ".";
    if (argc > 3) {
        fprintf(stderr, "Usage: %s [port [document-root]]\n", argv[0]);
        return 2;
    }
    if (argc >= 2) {
        char *end;
        port = strtol(argv[1], &end, 10);
        if (!argv[1][0] || *end || port < 1 || port > 65535) {
            fprintf(stderr, "Invalid port\n");
            return 2;
        }
    }
    if (argc == 3) document_root = argv[2];
    char root[PATH_MAX];
    if (!realpath(document_root, root)) { perror("document root"); return 1; }

    signal(SIGPIPE, SIG_IGN);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }
    int reuse = 1;
    (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(server, 16) < 0) {
        perror("bind/listen"); close(server); return 1;
    }
    printf("Steward HTTP serving %s on 0.0.0.0:%ld\n", root, port);
    fflush(stdout);
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept"); close(server); return 1;
        }
        serve(client, root);
        close(client);
    }
}
