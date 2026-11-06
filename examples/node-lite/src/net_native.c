#include "net_native.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <signal.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SEND_TIMEOUT_MS 5000

static JSValue neg_errno(JSContext *ctx)
{
    return JS_NewInt32(ctx, -errno);
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* listen(host, port, backlog) -> fd | -errno */
static JSValue net_listen(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    const char *host = JS_ToCString(ctx, argv[0]);
    int32_t port = 0, backlog = 128;
    JS_ToInt32(ctx, &port, argv[1]);
    JS_ToInt32(ctx, &backlog, argv[2]);
    if (!host) return JS_EXCEPTION;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (strcmp(host, "localhost") == 0) addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) addr.sin_addr.s_addr = htonl(INADDR_ANY);
    JS_FreeCString(ctx, host);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return neg_errno(ctx);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, backlog) < 0 || set_nonblocking(fd) < 0) {
        JSValue err = neg_errno(ctx);
        close(fd);
        return err;
    }
    return JS_NewInt32(ctx, fd);
}

/* port(fd) -> the bound port, useful after listen(host, 0) */
static JSValue net_port(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t fd;
    if (JS_ToInt32(ctx, &fd, argv[0])) return JS_EXCEPTION;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0) return neg_errno(ctx);
    return JS_NewInt32(ctx, ntohs(addr.sin_port));
}

/* number of leading one bits of a netmask */
static int prefix_length(const unsigned char *mask, int bytes)
{
    int bits = 0;
    for (int i = 0; i < bytes; i++) {
        for (unsigned char b = mask[i]; b & 0x80; b <<= 1) bits++;
    }
    return bits;
}

/* interfaces() -> [{ name, family, address, netmask, prefix, internal, scopeid }] for every IPv4 and
   IPv6 address of this machine (getifaddrs); os.networkInterfaces() groups them by name */
static JSValue net_interfaces(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) < 0) return neg_errno(ctx);
    JSValue arr = JS_NewArray(ctx);
    uint32_t index = 0;
    for (struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || !ifa->ifa_name) continue;
        int family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;

        char address[INET6_ADDRSTRLEN] = "", netmask[INET6_ADDRSTRLEN] = "";
        int prefix = 0, scope = 0;
        if (family == AF_INET) {
            struct sockaddr_in *a4 = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &a4->sin_addr, address, sizeof(address));
            if (ifa->ifa_netmask) {
                struct sockaddr_in *m4 = (struct sockaddr_in *)ifa->ifa_netmask;
                inet_ntop(AF_INET, &m4->sin_addr, netmask, sizeof(netmask));
                prefix = prefix_length((const unsigned char *)&m4->sin_addr, 4);
            }
        } else {
            struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            inet_ntop(AF_INET6, &a6->sin6_addr, address, sizeof(address));
            scope = (int)a6->sin6_scope_id;
            if (ifa->ifa_netmask) {
                struct sockaddr_in6 *m6 = (struct sockaddr_in6 *)ifa->ifa_netmask;
                inet_ntop(AF_INET6, &m6->sin6_addr, netmask, sizeof(netmask));
                prefix = prefix_length((const unsigned char *)&m6->sin6_addr, 16);
            }
        }

        JSValue item = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, item, "name", JS_NewString(ctx, ifa->ifa_name));
        JS_SetPropertyStr(ctx, item, "family", JS_NewString(ctx, family == AF_INET ? "IPv4" : "IPv6"));
        JS_SetPropertyStr(ctx, item, "address", JS_NewString(ctx, address));
        JS_SetPropertyStr(ctx, item, "netmask", JS_NewString(ctx, netmask));
        JS_SetPropertyStr(ctx, item, "prefix", JS_NewInt32(ctx, prefix));
        JS_SetPropertyStr(ctx, item, "internal", JS_NewBool(ctx, (ifa->ifa_flags & IFF_LOOPBACK) != 0));
        JS_SetPropertyStr(ctx, item, "scopeid", JS_NewInt32(ctx, scope));
        JS_SetPropertyUint32(ctx, arr, index++, item);
    }
    freeifaddrs(list);
    return arr;
}

/* accept(fd) -> [clientFd, ip, port] | null | -errno */
static JSValue net_accept(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t fd;
    if (JS_ToInt32(ctx, &fd, argv[0])) return JS_EXCEPTION;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int client = accept(fd, (struct sockaddr *)&addr, &len);
    if (client < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? JS_NULL : neg_errno(ctx);
    set_nonblocking(client);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    JSValue result = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, result, 0, JS_NewInt32(ctx, client));
    JS_SetPropertyUint32(ctx, result, 1, JS_NewString(ctx, ip));
    JS_SetPropertyUint32(ctx, result, 2, JS_NewInt32(ctx, ntohs(addr.sin_port)));
    return result;
}

/* recv(fd, max) -> ArrayBuffer (empty at end of stream) | null | -errno; works on sockets and pipes */
static JSValue net_recv(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t fd, max;
    if (JS_ToInt32(ctx, &fd, argv[0]) || JS_ToInt32(ctx, &max, argv[1])) return JS_EXCEPTION;
    if (max <= 0 || max > (1 << 20)) max = 65536;
    uint8_t *buf = malloc((size_t)max);
    if (!buf) return JS_ThrowOutOfMemory(ctx);
    ssize_t n = read(fd, buf, (size_t)max);
    JSValue result;
    if (n >= 0) result = JS_NewArrayBufferCopy(ctx, buf, (size_t)n);
    else if (errno == EAGAIN || errno == EWOULDBLOCK) result = JS_NULL;
    else result = neg_errno(ctx);
    free(buf);
    return result;
}

/* send(fd, arrayBuffer, offset, length) -> bytes sent | -errno; waits for the peer, up to a timeout */
static JSValue net_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t fd, offset, length;
    size_t size;
    if (JS_ToInt32(ctx, &fd, argv[0]) || JS_ToInt32(ctx, &offset, argv[2]) || JS_ToInt32(ctx, &length, argv[3]))
        return JS_EXCEPTION;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, argv[1]);
    if (!data) return JS_EXCEPTION;
    if (offset < 0 || length < 0 || (size_t)offset + (size_t)length > size)
        return JS_ThrowRangeError(ctx, "send: range outside the buffer");

    int32_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, data + offset + sent, (size_t)(length - sent), MSG_NOSIGNAL);
        if (n > 0) { sent += (int32_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd = { fd, POLLOUT, 0 };
            if (poll(&pfd, 1, SEND_TIMEOUT_MS) > 0) continue;
            errno = ETIMEDOUT;
        }
        if (n < 0 && errno == EINTR) continue;
        return neg_errno(ctx);
    }
    return JS_NewInt32(ctx, sent);
}

static JSValue net_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t fd;
    if (JS_ToInt32(ctx, &fd, argv[0])) return JS_EXCEPTION;
    return JS_NewInt32(ctx, close(fd) < 0 ? -errno : 0);
}

/* QuickJS' os.setReadHandler polls every handler but only dispatches among the first N entries (N = number
   of ready fds), so sockets are multiplexed through one epoll fd instead and a single handler is set on it. */

/* epoll() -> epoll fd | -errno */
static JSValue net_epoll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    int fd = epoll_create1(EPOLL_CLOEXEC);
    return JS_NewInt32(ctx, fd < 0 ? -errno : fd);
}

#define WATCH_READ 1
#define WATCH_WRITE 2
#define WATCH_ERROR 4

/* watch(epfd, fd, mask) -> 0 | -errno; mask is WATCH_READ | WATCH_WRITE, and adding an fd twice changes its mask */
static JSValue net_watch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t epfd, fd, mask;
    if (JS_ToInt32(ctx, &epfd, argv[0]) || JS_ToInt32(ctx, &fd, argv[1]) || JS_ToInt32(ctx, &mask, argv[2]))
        return JS_EXCEPTION;
    struct epoll_event ev = { .events = (mask & WATCH_READ ? EPOLLIN : 0) | (mask & WATCH_WRITE ? EPOLLOUT : 0),
                              .data = { .fd = fd } };
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0 && (errno != EEXIST || epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) < 0))
        return neg_errno(ctx);
    return JS_NewInt32(ctx, 0);
}

/* unwatch(epfd, fd) -> 0 | -errno */
static JSValue net_unwatch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t epfd, fd;
    if (JS_ToInt32(ctx, &epfd, argv[0]) || JS_ToInt32(ctx, &fd, argv[1])) return JS_EXCEPTION;
    struct epoll_event ev = { 0 };
    return JS_NewInt32(ctx, epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev) < 0 && errno != ENOENT ? -errno : 0);
}

/* wait(epfd) -> flat array [fd, flags, fd, flags, ...] of what is ready right now; flags use the WATCH_* bits */
static JSValue net_wait(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t epfd;
    if (JS_ToInt32(ctx, &epfd, argv[0])) return JS_EXCEPTION;
    struct epoll_event events[64];
    int n = epoll_wait(epfd, events, 64, 0);
    JSValue result = JS_NewArray(ctx);
    for (int i = 0; i < n; i++) {
        int flags = (events[i].events & EPOLLIN ? WATCH_READ : 0) | (events[i].events & EPOLLOUT ? WATCH_WRITE : 0) |
                    (events[i].events & (EPOLLERR | EPOLLHUP) ? WATCH_ERROR : 0);
        JS_SetPropertyUint32(ctx, result, (uint32_t)(2 * i), JS_NewInt32(ctx, events[i].data.fd));
        JS_SetPropertyUint32(ctx, result, (uint32_t)(2 * i + 1), JS_NewInt32(ctx, flags));
    }
    return result;
}

/* connect(ip, port) -> fd | -errno; the connection completes in the background, so wait for the fd to become
   writable and then ask connect_status(fd) */
static JSValue net_connect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    const char *ip = JS_ToCString(ctx, argv[0]);
    int32_t port;
    if (!ip) return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &port, argv[1])) { JS_FreeCString(ctx, ip); return JS_EXCEPTION; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    int valid = inet_pton(AF_INET, ip, &addr.sin_addr) == 1;
    JS_FreeCString(ctx, ip);
    if (!valid) { errno = EINVAL; return neg_errno(ctx); }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return neg_errno(ctx);
    if (set_nonblocking(fd) < 0 || (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 && errno != EINPROGRESS)) {
        JSValue err = neg_errno(ctx);
        close(fd);
        return err;
    }
    return JS_NewInt32(ctx, fd);
}

/* connect_status(fd) -> 0 when connected | -errno of the failed connection */
static JSValue net_connect_status(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t fd;
    if (JS_ToInt32(ctx, &fd, argv[0])) return JS_EXCEPTION;
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) return neg_errno(ctx);
    return JS_NewInt32(ctx, -error);
}

/* getaddrinfo blocks, so it runs on a thread that reports through a pipe the event loop can watch */
struct resolve_job {
    char host[256];
    int write_fd;
};

static void *resolve_thread(void *arg)
{
    struct resolve_job *job = arg;
    char out[1024];
    struct addrinfo hints, *list = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(job->host, NULL, &hints, &list);
    int len;
    if (rc != 0) {
        len = snprintf(out, sizeof(out), "err %s\n", gai_strerror(rc));
    } else {
        len = snprintf(out, sizeof(out), "ok");
        for (struct addrinfo *ai = list; ai && len < (int)sizeof(out) - 24; ai = ai->ai_next) {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in *)ai->ai_addr)->sin_addr, ip, sizeof(ip));
            len += snprintf(out + len, sizeof(out) - (size_t)len, " %s", ip);
        }
        len += snprintf(out + len, sizeof(out) - (size_t)len, "\n");
        freeaddrinfo(list);
    }
    if (write(job->write_fd, out, (size_t)len) < 0) { /* the reader went away */ }
    close(job->write_fd);
    free(job);
    return NULL;
}

/* resolve(host) -> fd to read "ok ip ip...\n" or "err message\n" from (then end of stream) | -errno */
static JSValue net_resolve(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    const char *host = JS_ToCString(ctx, argv[0]);
    if (!host) return JS_EXCEPTION;
    struct resolve_job *job = calloc(1, sizeof(*job));
    int fds[2];
    if (!job) { JS_FreeCString(ctx, host); return JS_ThrowOutOfMemory(ctx); }
    snprintf(job->host, sizeof(job->host), "%s", host);
    JS_FreeCString(ctx, host);

    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) < 0) { free(job); return neg_errno(ctx); }
    job->write_fd = fds[1];
    /* the writing end stays blocking: the answer is far smaller than the pipe buffer, so it never waits */
    fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL, 0) & ~O_NONBLOCK);

    pthread_t thread;
    int rc = pthread_create(&thread, NULL, resolve_thread, job);
    if (rc != 0) {
        close(fds[0]); close(fds[1]); free(job);
        errno = rc;
        return neg_errno(ctx);
    }
    pthread_detach(thread);
    return JS_NewInt32(ctx, fds[0]);
}

static const JSCFunctionListEntry net_funcs[] = {
    JS_CFUNC_DEF("epoll", 0, net_epoll),
    JS_CFUNC_DEF("watch", 3, net_watch),
    JS_CFUNC_DEF("unwatch", 2, net_unwatch),
    JS_CFUNC_DEF("wait", 1, net_wait),
    JS_CFUNC_DEF("connect", 2, net_connect),
    JS_CFUNC_DEF("connectStatus", 1, net_connect_status),
    JS_CFUNC_DEF("resolve", 1, net_resolve),
    JS_CFUNC_DEF("listen", 3, net_listen),
    JS_CFUNC_DEF("port", 1, net_port),
    JS_CFUNC_DEF("interfaces", 0, net_interfaces),
    JS_CFUNC_DEF("accept", 1, net_accept),
    JS_CFUNC_DEF("recv", 2, net_recv),
    JS_CFUNC_DEF("send", 4, net_send),
    JS_CFUNC_DEF("close", 1, net_close),
};

void node_lite_init_net(JSContext *ctx)
{
    signal(SIGPIPE, SIG_IGN);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue net = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, net, net_funcs, sizeof(net_funcs) / sizeof(net_funcs[0]));
    JS_SetPropertyStr(ctx, global, "__net", net);
    JS_FreeValue(ctx, global);
}
