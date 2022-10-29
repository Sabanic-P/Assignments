#include <unistd.h>
#include <stdio.h>
#include <malloc.h>
#include <pthread.h>
#include <stdbool.h>
#include <byteswap.h>
#include <stdint.h>
#include "alloc.h"

typedef struct memory_block
{
    void *start_address;
    bool free;
    uint32_t size;
    struct memory_block *next_block;
    struct memory_block *prev_block;

} memory_block_t;

memory_block_t mem_start;
pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;

void *_malloc(size_t size)
{
    pthread_mutex_lock(&memory_lock);
    if (size == 0)
        return NULL;
    if (mem_start.start_address == NULL)
    {
        void *start = sbrk(0);
        if(start == -1){
            pthread_mutex_unlock(&memory_lock);
            return NULL;
        }
        mem_start.start_address = start;
        void *new_end = sbrk(size);
        mem_start.free = false;
        mem_start.size = size;
        pthread_mutex_unlock(&memory_lock);
        return start;
    }
    else if (mem_start.free)
    {
        /*First element exists and is not taken*/
        if (mem_start.next_block == NULL)
        {

            /*Increase/Decrease first block to new size*/
            void* test = sbrk(size - mem_start.size);
            if(test == -1){
                pthread_mutex_unlock(&memory_lock);
                return NULL;
            }
            mem_start.size = size;
            mem_start.free = false;
            pthread_mutex_unlock(&memory_lock);
            return mem_start.start_address;
        }
        else if (size <= mem_start.size)
        {
            /*First block can be used for new allocation
             It is either being split up or used as is*/

            if (mem_start.size > size + sizeof(memory_block_t))
            {

                memory_block_t *new_block = mem_start.start_address + size;
                new_block->free = true;
                new_block->next_block = mem_start.next_block;
                new_block->prev_block = &mem_start;
                new_block->size = mem_start.size - size - sizeof(memory_block_t);
                new_block->start_address = new_block;
                mem_start.next_block->prev_block = new_block;
                mem_start.next_block = new_block;
                mem_start.size = size;
                mem_start.free = false;
                pthread_mutex_unlock(&memory_lock);
                return mem_start.start_address;
            }
            else
            {
                mem_start.free = false;
                pthread_mutex_unlock(&memory_lock);
                return mem_start.start_address;
            }
        }
    }

    /*First block is not empty
      traversing the list of blocks*/
    memory_block_t *m = &mem_start;
    for (; m->next_block != NULL; m = m->next_block)
    {
        if (m->next_block->free)
        {
            if (m->next_block->size >= size)
            {
                if (m->next_block->size > size + sizeof(memory_block_t))
                {
                    memory_block_t *new_block = m->next_block->start_address + sizeof(memory_block_t) + size;
                    new_block->free = true;
                    new_block->next_block = m->next_block->next_block;
                    new_block->prev_block = m->next_block;
                    new_block->size = m->next_block->size - size - sizeof(memory_block_t);
                    new_block->start_address = new_block;
                    if (m->next_block->next_block != NULL)
                        m->next_block->next_block->prev_block = new_block;
                    m->next_block->next_block = new_block;
                    m->next_block->size = size;
                }

                m->next_block->free = false;
                pthread_mutex_unlock(&memory_lock);
                return m->next_block->start_address + sizeof(memory_block_t);
            }
        }
    }

    /*No emtpy block with sufficant size was found*/

    /*Last block can be increased in size */
    if (m->free)
    {
        void* test = sbrk(size - m->size);
        if(test == -1){
            pthread_mutex_unlock(&memory_lock);
            return NULL;
        }
        m->size = size;
        m->free = false;
        pthread_mutex_unlock(&memory_lock);
        return m->start_address + sizeof(memory_block_t);
    }

    /*New block needs to be appended*/
    memory_block_t *new_block = sbrk(size + sizeof(memory_block_t));
    if(new_block == -1){
        pthread_mutex_unlock(&memory_lock);
        return NULL;
    }
    new_block->size = size;
    new_block->free = false;
    new_block->prev_block = m;
    new_block->next_block = NULL;
    new_block->start_address = new_block;
    new_block->prev_block->next_block = new_block;
    pthread_mutex_unlock(&memory_lock);

    return ((void *)new_block) + sizeof(memory_block_t);
}

void _free(void *ptr)
{
    pthread_mutex_lock(&memory_lock);
    /*Do nothing if no memory was allocated or NULL*/
    if (mem_start.start_address == NULL || ptr == NULL){
        pthread_mutex_unlock(&memory_lock);
        return;
    }
    if (ptr == mem_start.start_address)
    {
        if (mem_start.next_block == NULL)
        {
            sbrk(-mem_start.size);
            mem_start.start_address = NULL;
            mem_start.size = 0;
            mem_start.free = false;
            pthread_mutex_unlock(&memory_lock);
            return;            
        }
        else
        {
            mem_start.free = true;
        }
    }
    else
    {
        int found = false;
        for (memory_block_t *m = mem_start.next_block; m != NULL; m = m->next_block)
        {
            if (m->start_address + sizeof(memory_block_t) == ptr)
            {
                if(!m->free){
                    fprintf(stderr,"Double free\n");
                    exit(-1);
                }
                m->free = true;
                found = true;
                break;
            }
        }
        if(found){
            fprintf(stderr,"Invalid Pointer\n");
            exit(-1);
        }

        /*Combining empty memory blocks*/
        int prev_free = false;

        for (memory_block_t *m = mem_start.next_block; m != NULL; m = m->next_block)
        {
            if (m->free)
            {
                if (prev_free)
                {
                    memory_block_t *prev_block = m->prev_block;
                    prev_block->size += sizeof(memory_block_t) + m->size;
                    prev_block->next_block = m->next_block;
                    if (m->next_block != NULL)
                    {
                        m->next_block->prev_block = prev_block;
                    }
                }
                prev_free = true;
            }
            else
                prev_free = false;
        }

        /*Free last block if empty*/
        memory_block_t *l = mem_start.next_block;
        for (; l->next_block != NULL; l = l->next_block);
        
        if (l->free)
        {
            l->prev_block->next_block = NULL;
            sbrk(-l->size - sizeof(memory_block_t));
        }
    }

    if (mem_start.free)
    {
        /*Free all memory if first block is last one */
        // fprintf(stderr,"%d\n",mem_start.next_block->free);
        if (mem_start.next_block == NULL)
        {
            sbrk(-mem_start.size);
            mem_start.start_address = NULL;
            mem_start.size = 0;
            mem_start.free = false;
        }
        else
            /*Add all free blocks after the first one to the first one */
            if (mem_start.next_block->free)
            {
                // exit(0);
                mem_start.size += mem_start.next_block->size + sizeof(memory_block_t);
                if (mem_start.next_block->next_block != NULL)
                {
                    mem_start.next_block->next_block->prev_block = &mem_start;
                }
                mem_start.next_block = mem_start.next_block->next_block;
            }
    }

    pthread_mutex_unlock(&memory_lock);
}

void print_memory_layout()
{
    memory_block_t *m = &mem_start;
    int i = 0;
    for (; m != NULL; m = m->next_block)
    {
        fprintf(stderr, "Block %d: {size: %u, is_free: %d }\n", i++, m->size, m->free);
    }
}
#define malloc(x) _malloc(x)
#define free(x) _free(x)
