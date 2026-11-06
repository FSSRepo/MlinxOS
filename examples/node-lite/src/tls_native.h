#ifndef NODE_LITE_TLS_NATIVE_H
#define NODE_LITE_TLS_NATIVE_H

#include "quickjs.h"

/* Adds the TLS client primitives (tlsOpen, tlsHandshake, tlsRead, tlsWrite, tlsError, tlsClose) to the global
   __net object created by node_lite_init_net. Sockets stay owned and polled by JavaScript; mbedTLS only
   reads and writes the descriptor it is given. */
void node_lite_init_tls(JSContext *ctx);

#endif
