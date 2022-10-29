#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdatomic.h>
#include <signal.h>
#include "exchange.h"
#ifdef USECUSTOMMALLOC
#include "alloc.h"
#endif
#ifndef DONTPRINTEND
int total = 0;
#endif

typedef struct hash_table_entry
{
    uint32_t key;
    void *obj;
    uint32_t obj_length;
    struct hash_table_entry *next;
} entry_t;

typedef struct hash_table_entry_data
{
    entry_t entry;
    pthread_rwlock_t rwlock;
} entry_data_t;

typedef struct hashtable
{
    uint32_t table_size;
    entry_data_t table[];
} hashtable_t;

hashtable_t *table;
m_t *memory;
static volatile int running = 1;

hashtable_t *create_hashtable(uint32_t size)
{
    hashtable_t *t = malloc(sizeof(hashtable_t) + sizeof(entry_data_t) * size);
    memset(t, 0, sizeof(hashtable_t) + sizeof(entry_data_t) * size);
    t->table_size = size;
    for (int i = 0; i < size; i++)
    {
        pthread_rwlock_init(&t->table[i].rwlock, NULL);
    }
    return t;
}

void clear_entry_list(entry_t *curr)
{
    for (entry_t *n = curr; n != NULL; n = n->next)
    {
        free(n->obj);
    }
}

void clear_entries(entry_t *curr)
{
    void *tmp = NULL;
    for (; curr != NULL; curr = curr->next)
    {
        free(tmp);
#ifndef DONTPRINTEND
        total++;
#endif
        tmp = NULL;
        tmp = curr;
    }
    free(tmp);
}

void clear_hashtable(hashtable_t *t)
{
    for (int i = 0; i < t->table_size; i++)
    {
        clear_entry_list(&t->table[i].entry);
        clear_entries(t->table[i].entry.next);
    }
}

void insert(hashtable_t *t, uint32_t key, void *data, uint32_t data_length)
{

    uint32_t position = key % t->table_size;
    pthread_rwlock_wrlock(&t->table[position].rwlock);
    if (t->table[position].entry.obj == NULL)
    {
        t->table[position].entry.key = key;
        t->table[position].entry.obj = data;
        t->table[position].entry.obj_length = data_length;
        pthread_rwlock_unlock(&t->table[position].rwlock);
        return;
    }

    entry_t *current = &t->table[position].entry;
    for (; current->next != NULL; current = current->next);
    current->next = malloc(sizeof(entry_t));
    current = current->next;
    current->key = key;
    current->next = NULL;
    current->obj = data;
    current->obj_length = data_length;

    pthread_rwlock_unlock(&t->table[position].rwlock);
}

entry_t read_table(hashtable_t *t, uint32_t key)
{
    uint32_t position = key % t->table_size;
    pthread_rwlock_rdlock(&t->table[position].rwlock);
    entry_t r;
    for (entry_t *current = &t->table[position].entry; current != NULL; current = current->next)
    {
        if (current->key == key)
        {
            r = *current;
            r.obj = malloc(r.obj_length);
            memcpy(r.obj, current->obj, r.obj_length);
            pthread_rwlock_unlock(&t->table[position].rwlock);
            return r;
        }
    }
    pthread_rwlock_unlock(&t->table[position].rwlock);
    return (entry_t){.obj = NULL, .obj_length = 0};
}

void *delete(hashtable_t *t, uint32_t key)
{

    uint32_t position = key % t->table_size;
    void *r;
    pthread_rwlock_wrlock(&t->table[position].rwlock);
    if (t->table[position].entry.key == key && t->table[position].entry.obj != NULL)
    {
        r = t->table[position].entry.obj;
        if (t->table[position].entry.next != NULL)
        {
            entry_t *next = t->table[position].entry.next;
            t->table[position].entry = *next;
            free(next);
        }
        else
        {
            t->table[position].entry = (entry_t){.obj = NULL, .obj_length = 0};
        }
        pthread_rwlock_unlock(&t->table[position].rwlock);

        return r;
    }

    for (entry_t *current = &t->table[position].entry; current->next != NULL; current = current->next)
    {
        if (current->next->key == key)
        {
            entry_t *next = current->next;
            r = next->obj;
            current->next = current->next->next;
            pthread_rwlock_unlock(&t->table[position].rwlock);
            free(next);
            return r;
        }
    }

    pthread_rwlock_unlock(&t->table[position].rwlock);
    return NULL;
}

void init_memory_region(m_t *r, size_t size)
{
    r->client_count = 0;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&r->id_lock, &attr);

    pthread_condattr_t condattr;
    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);

    for (int i = 0; i < CLIENT_SLOTS; i++)
    {
        pthread_mutex_init(&r->c_slots[i].rw, &attr);
        pthread_mutex_init(&r->c_slots[i].cond_mutex, &attr);
        pthread_cond_init(&r->c_slots[i].cond, &condattr);
    }

    pthread_mutexattr_destroy(&attr);
    pthread_condattr_destroy(&condattr);
}


/*Function is used for the threads
  Each thread serves one slot in the shared memory
*/
void *serve_function(void *args)
{
    uint64_t id = (uintptr_t)args;
    pthread_mutex_t *cond_mutex = &memory->c_slots[id].cond_mutex;
    pthread_cond_t *cond = &memory->c_slots[id].cond;
    exchange_t *e = &memory->c_slots[id];
    int i = 0;

    while (running)
    {
        pthread_mutex_lock(cond_mutex);

        while (e->type == NO_REQUEST)
        {
            pthread_cond_wait(cond, cond_mutex);
            if (!running)
            {
                pthread_mutex_unlock(cond_mutex);
                return NULL;
            }
        }
        void *data;
        entry_t entry;
        switch (e->type)
        {
        case REQUEST_INSERT:
            data = malloc(e->length);
            memcpy(data, e->data, e->length);
            insert(table, e->key, data, e->length);
            break;

        case REQUEST_DELETE:
            data = delete (table, e->key);
            if (data == NULL)
                fprintf(stderr, "Element not found %d %d\n", e->key, id);
            free(data);
            break;

        case REQUEST_READ:
            entry = read_table(table, e->key);
            memcpy(e->data, entry.obj, entry.obj_length);
            free(entry.obj);
            e->length = entry.obj_length;

            break;

        default:
            fprintf(stderr, "Unexpected type (%d) received on Position %d\n", e->type, id);
        }
        e->type = NO_REQUEST;

        pthread_cond_signal(cond);
        pthread_mutex_unlock(cond_mutex);
    }
    return NULL;
}

void stop_exec(int sig)
{
    pthread_mutex_destroy(&memory->id_lock);
    running = 0;
    for (int i = 0; i < CLIENT_SLOTS; i++)
    {
        pthread_cond_signal(&memory->c_slots[i].cond);
    }
    signal(SIGINT, SIG_DFL);
}

int main(int argc, char **argv)
{
    uint32_t table_size = 100;
    if (argc < 2)
    {
        printf("No table size given defaulting to 100\n");
    }
    else
    {
        table_size = strtoul(argv[1], NULL, 10);
    }
    table = create_hashtable(table_size);
    int s = shm_open("shared-mem", O_RDWR | O_CREAT, 0777);
    shm_unlink("shared-mem");
    s = shm_open("shared-mem", O_RDWR | O_CREAT, 0777);
    int size = (sizeof(exchange_t)) * CLIENT_SLOTS;

    ftruncate(s, size);
    memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, s, 0);
    if (memory == MAP_FAILED)
    {
        fprintf(stderr, "mmap failed\n");
        return -1;
    }
    signal(SIGINT, stop_exec);
    init_memory_region(memory, size);

    pthread_t t[CLIENT_SLOTS];

    for (uint64_t i = 0; i < CLIENT_SLOTS; i++)
        pthread_create(&t[i], NULL, serve_function, (void*)((uintptr_t)i));

    for (uint64_t i = 0; i < CLIENT_SLOTS; i++)
        pthread_join(t[i], NULL);

    for (int i = 0; i < CLIENT_SLOTS; i++)
    {
        pthread_mutex_destroy(&memory->c_slots[i].rw);
        pthread_mutex_destroy(&memory->c_slots[i].cond_mutex);
        pthread_cond_destroy(&memory->c_slots[i].cond);
    }

    close(s);
    shm_unlink("shared-mem");

    clear_hashtable(table);
    free(table);
#ifndef DONTPRINTEND
    fprintf(stderr, "\nEntries left in table after after all clients finished: %d\n", total);
#else
    fprintf(stderr, "End of execution\n");
#endif
    return 0;
}