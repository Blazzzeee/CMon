#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>

extern size_t arena_buf_size;
extern size_t arena_buf_num;

#define BITMAP_WORDS (arena_buf_num / 64)

// Configures the arena size and number of buffers
void arena_config(size_t buf_size, size_t buf_num);
// Initializes the arena
void *prealloc_arena();

// Allocates a buffer of 'req' bytes (max BUF_SIZE)
void *allocate(size_t req);

// Deallocates a pointer
void deallocate(void *ptr);

// Tears down execution
void teardown_arena();

int find_k_consecutive_zeroes(uint64_t free_mask , int k);

#endif
