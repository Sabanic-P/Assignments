#include "exchange.h"
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#ifdef USECUSTOMMALLOC
#include "alloc.h"
#endif

#define DATALENGTH 1024
int id;

void insert(void *data, uint32_t key, uint32_t data_length, pthread_cond_t *cond, pthread_mutex_t *cond_mutex, exchange_t *e)
{
    pthread_mutex_lock(cond_mutex);
    memcpy(e->data, data, data_length);
    e->key = key;
    e->length = data_length;
    e->type = REQUEST_INSERT;

    pthread_cond_signal(cond);
    while (e->type != NO_REQUEST)
    {
        pthread_cond_wait(cond, cond_mutex);
    }
    pthread_mutex_unlock(cond_mutex);
}

void mem_read(void *data, uint32_t key, uint32_t data_length, pthread_cond_t *cond, pthread_mutex_t *cond_mutex, exchange_t *e)
{

    pthread_mutex_lock(cond_mutex);
    memcpy(e->data, data, data_length);
    e->key = key;
    e->type = REQUEST_READ;
    pthread_cond_signal(cond);
    while (e->type != NO_REQUEST)
    {
        pthread_cond_wait(cond, cond_mutex);
    }
    if (data_length != e->length)
    {
        fprintf(stderr, "Received unexpected data length\n");
        pthread_mutex_unlock(cond_mutex);
        return;
    }
    memcpy(data, e->data, e->length);
    pthread_mutex_unlock(cond_mutex);
}

void delete(uint32_t key, pthread_cond_t *cond, pthread_mutex_t *cond_mutex, exchange_t *e)
{

    pthread_mutex_lock(cond_mutex);
    e->key = key;
    e->type = REQUEST_DELETE;
    pthread_cond_signal(cond);
    while (e->type != NO_REQUEST)
    {
        pthread_cond_wait(cond, cond_mutex);
    }
    pthread_mutex_unlock(cond_mutex);
}

/*
Client is used to the test the server and the client functionality
*/
int main(int argc, char **argv)
{
    /*test_size must be the same on all clients for all clients running*/
    uint32_t test_size;
    if (argc < 2)
    {
        test_size = DATALENGTH;
    }
    else
    {
        test_size = strtoul(argv[1], NULL, 10);
        test_size = test_size > MAX_TRANSMISSION_SIZE ? MAX_TRANSMISSION_SIZE : test_size;
    }

    int **arr = malloc(sizeof(int *) * test_size);
    int **arr_cmp = malloc(sizeof(int *) * test_size);
    for (int i = 0; i < test_size; i++)
    {
        arr[i] = malloc(test_size * sizeof(int));
        arr_cmp[i] = malloc(test_size * sizeof(int));
    }

    srand(0);
    for (int i = 0; i < test_size; i++)
    {
        for (int j = 0; j < test_size; j++)
        {
            arr[i][j] = rand();
        }
    }

    int size = (sizeof(exchange_t)) * CLIENT_SLOTS;
    int s = shm_open("shared-mem", O_RDWR, 0777);
    if (s < 0)
    {
        fprintf(stderr, "Failed to open memory: %s\n", strerror(errno));
        return -1;
    }

    m_t *memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, s, 0);

    pthread_mutex_lock(&memory->id_lock);
    int ID = memory->client_count++;
    int id = ID % CLIENT_SLOTS;
    fprintf(stdout, "Client ID: %d, Test size: %d\n", id, test_size);
    pthread_mutex_unlock(&memory->id_lock);

    pthread_mutex_t *cond_mutex = &memory->c_slots[id].cond_mutex;
    pthread_cond_t *cond = &memory->c_slots[id].cond;
    exchange_t *e = &memory->c_slots[id];
    int i = 0;

    printf("Client %d: Insert Test\n", ID);
    for (int i = 0; i < test_size; i++)
    {
        pthread_mutex_lock(&e->rw);
        insert(arr[i], test_size * ID + i, test_size * 4, cond, cond_mutex, e);
        pthread_mutex_unlock(&e->rw);
    }

    printf("Client %d: Read Test\n", ID);
    for (int i = test_size - 1; i > -1; i--)
    {
        pthread_mutex_lock(&e->rw);
        mem_read(arr_cmp[i], test_size * ID + i, test_size * 4, cond, cond_mutex, e);
        pthread_mutex_unlock(&e->rw);
    }
    bool found_mismatch = false;
    for (int i = 0; i < test_size; i++)
    {
        for (int j = 0; j < test_size; j++)
        {
            if (arr[i][j] != arr_cmp[i][j])
            {
                found_mismatch = true;
                fprintf(stderr, "Client %d: Values do not match\n", ID);
            }
        }
    }
    printf("Client %d: Delete Test\n", ID);

    for (int i = 0; i < test_size; i++)
    {
        pthread_mutex_lock(&e->rw);
        delete (test_size * ID + i, cond, cond_mutex, e);
        pthread_mutex_unlock(&e->rw);
    }

    for (int i = 0; i < test_size; i++)
    {
        free(arr[i]);
        free(arr_cmp[i]);
    }
    free(arr);
    free(arr_cmp);

    if (!found_mismatch)
        fprintf(stderr, "Client %d finished succesfully\n", ID);
    else
        fprintf(stderr, "Client %d finished with incorrect values\n", ID);
}