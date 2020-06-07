#ifndef __LISTENER_H__
#define __LISTENER_H__

#ifdef __cplusplus
extern "C" {
#endif

#define LISTENER_ADDRESS "tcp://localhost:5556"

struct Listener {
    void* context;
    void* subscriber;
};

int Listener_init();
void Listener_destruct();
char* Listener_poll_message();

#ifdef __cplusplus
}
#endif

#endif /* __LISTENER_H__ */