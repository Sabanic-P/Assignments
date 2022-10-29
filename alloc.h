#ifndef ALLOC_H
#define ALLOC_H
#include <unistd.h>
#include <stdio.h>
#include <malloc.h>
#include <pthread.h>
#include <stdbool.h>
#include <byteswap.h>
#include <stdint.h>


void *_malloc(size_t size);
void _free(void *ptr);
void print_memory_layout();

#define malloc(x) _malloc(x)
#define free(x) _free(x)

#endif