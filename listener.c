#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <czmq.h>

#include "listener.h"

static struct Listener listener;

int Listener_init()
{
    listener.context = zmq_ctx_new();
    listener.subscriber = zmq_socket(listener.context, ZMQ_SUB);
    int rc = zmq_connect(listener.subscriber, LISTENER_ADDRESS);
    assert(rc == 0);
    rc = zmq_setsockopt(listener.subscriber, ZMQ_SUBSCRIBE, "LED", 3);
    assert(rc == 0);
    printf("Connected\n");
    return 0;
}

void Listener_destruct()
{
    zmq_close(listener.subscriber);
    zmq_ctx_destroy(listener.context);
}

char* Listener_poll_message()
{
    char buffer[1024];
    int size = zmq_recv(listener.subscriber, buffer, 1023, ZMQ_DONTWAIT);
    if (size == -1)
        return NULL;
    if (size > 1023)
        size = 1023;
    buffer[size] = 0x0;
#ifdef __linux__
    return strndup(buffer, sizeof(buffer) - 1);
#else
    return strdup(buffer);
#endif // __linux__
}

