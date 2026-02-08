#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>

#define BUF_SIZE (256) 

#define BUF_NUM 64

#define BITMAP_WORDS (BUF_NUM / 64)
// Initializes the arena
void *prealloc_arena();

// Allocates a buffer of 'req' bytes (max BUF_SIZE)
void *allocate(size_t req);

// Deallocates a pointer
void deallocate(void *ptr);

// Tears down execution
void teardown_arena();

int find_k_consecutive_zeroes(int k);


#endif
