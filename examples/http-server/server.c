#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int send_all(int fd, const char *data, size_t length)
{
    while (length) {
        ssize_t n = send(fd, data, length, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        data += n;
        length -= (size_t)n;
    }
    return 0;
}

static void serve(int fd)
{
    char request[4096], method[16], path[2048], version[16];
    size_t used = 0;
    struct timeval timeout = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    while (used < sizeof(request) - 1) {
        ssize_t n = recv(fd, request + used, sizeof(request) - 1 - used, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        used += (size_t)n;
        request[used] = '\0';
        if (strstr(request, "\r\n\r\n")) break;
    }
    const char *status = "400 Bad Request";
    const char *type = "text/plain; charset=utf-8";
    const char *body = "Bad request\n";
    int head = 0;
    if (strstr(request, "\r\n\r\n") &&
        sscanf(request, "%15s %2047s %15s", method, path, version) == 3 &&
        (!strcmp(version, "HTTP/1.0") || !strcmp(version, "HTTP/1.1"))) {
        head = !strcmp(method, "HEAD");
        if (strcmp(method, "GET") && !head) {
            status = "405 Method Not Allowed";
            body = "Method not allowed\n";
        } else if (!strcmp(path, "/")) {
            status = "200 OK";
            type = "text/html; charset=utf-8";
            body = "<!doctype html><html lang=\"es\"><meta charset=\"utf-8\">"
                   "<title>Steward OS</title><h1>Steward OS</h1>"
                   "<p>Servidor HTTP en C funcionando.</p>"
                   "<p><a href=\"/health\">Estado</a></p></html>\n";
        } else if (!strcmp(path, "/health")) {
            status = "200 OK";
            type = "application/json";
            body = "{\"status\":\"ok\",\"system\":\"Steward OS\"}\n";
        } else {
            status = "404 Not Found";
            body = "Not found\n";
        }
    }
    char headers[512];
    int length = snprintf(headers, sizeof(headers),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\n%s\r\n", status, type, strlen(body),
        !strcmp(status, "405 Method Not Allowed") ? "Allow: GET, HEAD\r\n" : "");
    if (length > 0 && (size_t)length < sizeof(headers) &&
        send_all(fd, headers, (size_t)length) == 0 && !head)
        send_all(fd, body, strlen(body));
}

int main(int argc, char **argv)
{
    long port = 8080;
    if (argc > 2) return 2;
    if (argc == 2) {
        char *end;
        port = strtol(argv[1], &end, 10);
        if (!argv[1][0] || *end || port < 1 || port > 65535) {
            fprintf(stderr, "Invalid port\n");
            return 2;
        }
    }
    signal(SIGPIPE, SIG_IGN);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(server, 16) < 0) {
        perror("bind/listen"); close(server); return 1;
    }
    printf("Steward HTTP listening on 0.0.0.0:%ld\n", port);
    fflush(stdout);
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept"); close(server); return 1;
        }
        serve(client);
        close(client);
    }
}
