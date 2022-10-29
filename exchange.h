#ifndef EXCHANGE_H
#define EXCHANGE_H

#include <stdint.h>
#include <pthread.h>

#define CLIENT_SLOTS 20
#define MAX_TRANSMISSION_SIZE 4096

typedef enum {
    NO_REQUEST = 0,
    REQUEST_INSERT,
    REQUEST_READ,
    REQUEST_DELETE,
}request_type;

typedef struct exchange{
    pthread_mutex_t rw;
    pthread_mutex_t cond_mutex;
    pthread_cond_t  cond;
    request_type type;
    
    uint32_t key;
    uint32_t length;
    uint32_t data[MAX_TRANSMISSION_SIZE];
}exchange_t;

typedef struct memory_exchange{
    pthread_mutex_t id_lock;
    uint32_t client_count;
    exchange_t c_slots[CLIENT_SLOTS];
} m_t;

#endif