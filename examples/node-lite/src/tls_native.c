#include "tls_native.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

/* Mozilla's root certificates, embedded at build time from examples/external/ca/cacert.pem */
extern const unsigned char node_lite_ca_pem[];
extern const unsigned long node_lite_ca_pem_len;

#define MAX_CONNECTIONS 256
#define IO_TIMEOUT_MS 15000

struct tls_conn {
    mbedtls_ssl_context ssl;
    int fd;
    char error[256];
};

static struct tls_conn *connections[MAX_CONNECTIONS];

static int global_state; /* 0 not initialised, 1 ready, -1 failed */
static char global_error[256];
static mbedtls_ssl_config conf;
static mbedtls_x509_crt ca_chain;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_entropy_context entropy;

static void describe(char *out, size_t size, const char *what, int rc)
{
    char reason[160];
    mbedtls_strerror(rc, reason, sizeof(reason));
    snprintf(out, size, "%s: %s (-0x%04x)", what, reason, (unsigned)-rc);
}

static int global_init(void)
{
    if (global_state) return global_state;
    global_state = -1;

    psa_crypto_init(); /* TLS 1.3 key schedule goes through PSA */
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&ca_chain);
    mbedtls_ssl_config_init(&conf);

    int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (rc) { describe(global_error, sizeof(global_error), "random generator", rc); return -1; }

    /* certificates that use algorithms this build lacks are skipped (a positive return value counts them) */
    rc = mbedtls_x509_crt_parse(&ca_chain, node_lite_ca_pem, node_lite_ca_pem_len + 1);
    if (rc < 0) { describe(global_error, sizeof(global_error), "root certificates", rc); return -1; }
    const char *extra = getenv("SSL_CERT_FILE");
    if (extra && *extra) mbedtls_x509_crt_parse_file(&ca_chain, extra);

    rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc) { describe(global_error, sizeof(global_error), "TLS configuration", rc); return -1; }
    const char *insecure = getenv("NODE_TLS_REJECT_UNAUTHORIZED");
    mbedtls_ssl_conf_authmode(&conf, insecure && strcmp(insecure, "0") == 0 ? MBEDTLS_SSL_VERIFY_NONE : MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &ca_chain, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);

    global_state = 1;
    return 1;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    struct tls_conn *c = ctx;
    ssize_t n = send(c->fd, buf, len, MSG_NOSIGNAL);
    if (n >= 0) return (int)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    struct tls_conn *c = ctx;
    ssize_t n = read(c->fd, buf, len);
    if (n >= 0) return (int)n; /* 0 is the end of the stream */
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static struct tls_conn *lookup(JSContext *ctx, JSValueConst value, int32_t *id_out)
{
    int32_t id;
    if (JS_ToInt32(ctx, &id, value)) return NULL;
    if (id_out) *id_out = id;
    return id >= 0 && id < MAX_CONNECTIONS ? connections[id] : NULL;
}

static JSValue neg(JSContext *ctx, int value)
{
    return JS_NewInt32(ctx, value);
}

/* tlsOpen(fd, hostname) -> handle | -1 (see tlsError(-1)) */
static JSValue tls_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t fd;
    if (JS_ToInt32(ctx, &fd, argv[0])) return JS_EXCEPTION;
    const char *host = JS_ToCString(ctx, argv[1]);
    if (!host) return JS_EXCEPTION;

    if (global_init() < 0) { JS_FreeCString(ctx, host); return neg(ctx, -1); }
    int id = 0;
    while (id < MAX_CONNECTIONS && connections[id]) id++;
    struct tls_conn *c = id < MAX_CONNECTIONS ? calloc(1, sizeof(*c)) : NULL;
    if (!c) {
        snprintf(global_error, sizeof(global_error), "too many TLS connections");
        JS_FreeCString(ctx, host);
        return neg(ctx, -1);
    }
    c->fd = fd;
    mbedtls_ssl_init(&c->ssl);
    int rc = mbedtls_ssl_setup(&c->ssl, &conf);
    if (!rc) rc = mbedtls_ssl_set_hostname(&c->ssl, host);
    JS_FreeCString(ctx, host);
    if (rc) {
        describe(global_error, sizeof(global_error), "TLS setup", rc);
        mbedtls_ssl_free(&c->ssl);
        free(c);
        return neg(ctx, -1);
    }
    mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, NULL);
    connections[id] = c;
    return JS_NewInt32(ctx, id);
}

/* tlsHandshake(handle) -> 0 done | 1 wait until readable | 2 wait until writable | -1 failed (see tlsError) */
static JSValue tls_handshake(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    struct tls_conn *c = lookup(ctx, argv[0], NULL);
    if (!c) return neg(ctx, -1);
    int rc = mbedtls_ssl_handshake(&c->ssl);
    if (rc == 0) return neg(ctx, 0);
    if (rc == MBEDTLS_ERR_SSL_WANT_READ) return neg(ctx, 1);
    if (rc == MBEDTLS_ERR_SSL_WANT_WRITE) return neg(ctx, 2);

    describe(c->error, sizeof(c->error), "TLS handshake failed", rc);
    if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        char info[200];
        mbedtls_x509_crt_verify_info(info, sizeof(info), "", mbedtls_ssl_get_verify_result(&c->ssl));
        size_t n = strlen(info);
        while (n && (info[n - 1] == '\n' || info[n - 1] == ' ')) info[--n] = '\0';
        snprintf(c->error, sizeof(c->error), "certificate verification failed: %s", info);
    }
    return neg(ctx, -1);
}

/* tlsRead(handle, max) -> ArrayBuffer (empty at the end of the stream) | null when nothing is available yet | -1 */
static JSValue tls_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    struct tls_conn *c = lookup(ctx, argv[0], NULL);
    int32_t max;
    if (JS_ToInt32(ctx, &max, argv[1])) return JS_EXCEPTION;
    if (!c) return neg(ctx, -1);
    if (max <= 0 || max > (1 << 20)) max = 16384;

    unsigned char *buf = malloc((size_t)max);
    if (!buf) return JS_ThrowOutOfMemory(ctx);
    int rc;
    do {
        rc = mbedtls_ssl_read(&c->ssl, buf, (size_t)max);
    } while (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);

    JSValue result;
    if (rc > 0) result = JS_NewArrayBufferCopy(ctx, buf, (size_t)rc);
    else if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == MBEDTLS_ERR_SSL_CONN_EOF)
        result = JS_NewArrayBufferCopy(ctx, buf, 0);
    else if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) result = JS_NULL;
    else {
        describe(c->error, sizeof(c->error), "TLS read failed", rc);
        result = neg(ctx, -1);
    }
    free(buf);
    return result;
}

/* tlsWrite(handle, arrayBuffer, offset, length) -> length | -1; waits for the peer, up to a timeout */
static JSValue tls_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    struct tls_conn *c = lookup(ctx, argv[0], NULL);
    int32_t offset, length;
    size_t size;
    if (JS_ToInt32(ctx, &offset, argv[2]) || JS_ToInt32(ctx, &length, argv[3])) return JS_EXCEPTION;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, argv[1]);
    if (!data) return JS_EXCEPTION;
    if (!c) return neg(ctx, -1);
    if (offset < 0 || length < 0 || (size_t)offset + (size_t)length > size)
        return JS_ThrowRangeError(ctx, "tlsWrite: range outside the buffer");

    int32_t sent = 0;
    while (sent < length) {
        int rc = mbedtls_ssl_write(&c->ssl, data + offset + sent, (size_t)(length - sent));
        if (rc > 0) { sent += rc; continue; }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
            struct pollfd pfd = { c->fd, rc == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT, 0 };
            if (poll(&pfd, 1, IO_TIMEOUT_MS) > 0) continue;
            snprintf(c->error, sizeof(c->error), "TLS write timed out");
        } else {
            describe(c->error, sizeof(c->error), "TLS write failed", rc);
        }
        return neg(ctx, -1);
    }
    return JS_NewInt32(ctx, sent);
}

/* tlsError(handle) -> text of the last failure; -1 asks about tlsOpen */
static JSValue tls_error(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    struct tls_conn *c = lookup(ctx, argv[0], NULL);
    return JS_NewString(ctx, c ? c->error : global_error);
}

/* tlsClose(handle): sends close_notify when possible and frees the session; the descriptor stays open */
static JSValue tls_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    int32_t id;
    struct tls_conn *c = lookup(ctx, argv[0], &id);
    if (!c) return neg(ctx, 0);
    mbedtls_ssl_close_notify(&c->ssl);
    mbedtls_ssl_free(&c->ssl);
    free(c);
    connections[id] = NULL;
    return neg(ctx, 0);
}

static const JSCFunctionListEntry tls_funcs[] = {
    JS_CFUNC_DEF("tlsOpen", 2, tls_open),
    JS_CFUNC_DEF("tlsHandshake", 1, tls_handshake),
    JS_CFUNC_DEF("tlsRead", 2, tls_read),
    JS_CFUNC_DEF("tlsWrite", 4, tls_write),
    JS_CFUNC_DEF("tlsError", 1, tls_error),
    JS_CFUNC_DEF("tlsClose", 1, tls_close),
};

void node_lite_init_tls(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue net = JS_GetPropertyStr(ctx, global, "__net");
    JS_SetPropertyFunctionList(ctx, net, tls_funcs, sizeof(tls_funcs) / sizeof(tls_funcs[0]));
    JS_FreeValue(ctx, net);
    JS_FreeValue(ctx, global);
}
