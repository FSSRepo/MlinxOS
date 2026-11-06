#ifndef NODE_LITE_NET_NATIVE_H
#define NODE_LITE_NET_NATIVE_H

#include "quickjs.h"

/* Adds the global object __net (TCP primitives over file descriptors) that lib/node-lite.js builds
   the http module on. Calls return -errno on failure, null when they would block. */
void node_lite_init_net(JSContext *ctx);

#endif
