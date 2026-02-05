#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

// Individual buffer size is 256 bytes each
#define BUF_SIZE 256
// Define number of buffers
// since we associate each bit of
// 64 bit integer to each buffer 
#define BUF_NUM 64


// Used to initialise the arena
// performs all the necessary operations that will be needed
// before allocationg  a request
void *prealloc_arena();


void *allocate(size_t req);


void deallocate(void *ptr);


void teardown_arena();

void *check_and_claim_atomically(size_t req);

int find_k_consecutive_zeroes(int k);
