#include "arena.h"
#include <cstdint>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef BUF_SIZE
// Crash program
#endif

#ifndef BUF_NUM
// Crash program
#endif

// Means there can only exist one arena at a time, since the state is global
static uint64_t LOCK = 0;
// Note this lock should only be used for
// tracking chunks in buf , not for Synchronization
// in other words this is the internal book keeping
// data structhure of our allocator
static void *BUF;

typedef struct {
    uint16_t chunks;
} arena_hdr_t;

#define REQUIRE_ARENA_OR_RETURN_NULL()                                                             \
    do {                                                                                           \
        if (BUF == NULL) {                                                                         \
            fprintf(stderr, "FATAL: Arena not initialized\n");                                     \
            return NULL;                                                                           \
        }                                                                                          \
    } while (0)

// Initialises arena for future allocations
void *prealloc_arena() {

    void *tmp;
    size_t total_size = BUF_SIZE * BUF_NUM;
    // Reset lock to zero
    LOCK = 0;

    // Initialise global memory , that will be used for arena allocations
    tmp = malloc(total_size);

    if (!tmp) {
        fprintf(stderr, "prealloc_arna: malloc\n");
        // Every allocation after this should fail
        return NULL;
    } else {
        BUF = tmp;
        memset(BUF, 0, total_size);
    }

    return BUF;
}

// This function finds k consecutive zeroes in our bitmap , if the req matches k then the buffer
// will be atomically claimed and a base pointer will be returned
void *check_and_claim(size_t req) {
    // Convert bytes to number of chunks
    // We add metadata to keep track of allocation length
    size_t need = req + sizeof(arena_hdr_t);
    uint64_t k = (need + BUF_SIZE - 1) / BUF_SIZE;

    int start_bit = find_k_consecutive_zeroes(k);
    if (start_bit == -1)
        return NULL;

    // Create a mask of K bits set to 1
    // e.g., if k=3, mask is 0...0111
    uint64_t claim_mask = (k == 64) ? ~0ULL : (1ULL << k) - 1;

    // Shift it to the correct position
    claim_mask <<= start_bit;

    // Mark these bits as USED (set to 1) in our global lock
    // Use __atomic_fetch_or for true thread safety if needed
    LOCK |= claim_mask;

    char *base = (char *)BUF + (start_bit * BUF_SIZE);
    // Internal 2 byte book keeping structhure enforced in
    // allocations , which helps in deallocation
    arena_hdr_t *hdr = (arena_hdr_t *)base;
    hdr->chunks = (uint16_t)k;

    // Return the pointer: , with arena_hdr_t
    return (void *)(base + sizeof(arena_hdr_t));
}

// O(1) running tine for given K ,
// use bit smear algo on the bitmask
int find_k_consecutive_zeroes(int k) {

    if (k <= 0)
        return -1;
    uint64_t free_mask, combined;
    free_mask = ~LOCK;

    if (free_mask == 0) {
        fprintf(stderr, "FATAL: k_zeroes: Buffer overflow\n");
        return -1;
    }

    combined = free_mask;

    // We shift and AND the mask k-1 times.
    // After k-1 iterations, any bit still set to 1 in 'combined'
    // represents the START of a sequence of k consecutive 1s in free_mask.
    for (int i = 1; i < k; ++i) {
        combined &= (combined >> 1);
    }

    // The free bits are marked as 1
    if (combined == 0) {
        fprintf(stderr, "FATAL: no k-consecutive-zeroes\n");
        return -1;
    }

    // Returns first free index
    return __builtin_ctz(combined);
}

// O(1) allocation
// Allocates req bytes in arena , the book keeping is done by
// using the static lock 64 bit integer
// variable
void *allocate(size_t req) {
    void *tmp;
    REQUIRE_ARENA_OR_RETURN_NULL();

    if (req <= 0) {
        fprintf(stderr, "FATAl: allocate: req bytes must be positive\n");
        return NULL;
    }

    // Find number of chunks that will be used for this allocation
    // Check if the chunks are available , if yes then mark them in use
    // , along with returning a base pointer to the start of the requested memory chunk
    tmp = check_and_claim(req);

    return tmp;
}

// O(1) deallocation
// Mark the chunk to be out of use , can be used for future allocations
// NOTE: we must clear the entire length of the buf by matching it with how many bitmasks were
// this means we must deterministically predict the bitmask bits that were marked previously
// To achieve that we enforce a constraint that before 2 bytes before the allocated chunk is
// number of chunks (k) that were allocated, now if we ensure that every allocation has
// this , it becomes certain that we can deterministically determine the buffer boundary , or the
// bitmask to turn off
void deallocate(void *ptr) {

    if (!ptr) {
        return;
    }

    char *user_ptr = (char *)ptr;

    // Read header
    arena_hdr_t *hdr = (arena_hdr_t *)(user_ptr - sizeof(arena_hdr_t));
    uint64_t k = hdr->chunks;

    // Sanity checks

    // Protect from random pointers
    if ((char *)hdr < (char *)BUF || (char *)hdr >= (char *)BUF + BUF_SIZE * BUF_NUM) {
        fprintf(stderr, "arena_free: invalid pointer\n");
        return;
    }

    // If metadata was corrupted , we cant deallocate
    if (k == 0 || k > BUF_NUM || k > 64) {
        fprintf(stderr, "arena_free: corrupted header (k=%lu)\n", k);
        return;
    }

    // end of allocation fits
    char *end = (char *)hdr + k * BUF_SIZE;
    if (end > (char *)BUF + BUF_SIZE * BUF_NUM) {
        fprintf(stderr, "arena_free: allocation overruns arena\n");
        return;
    }

    // Compute chunk start
    size_t offset = (char *)hdr - (char *)BUF;
    uint64_t start_bit = offset / BUF_SIZE;

    // Build mask
    uint64_t mask = ((k == 64) ? ~0ULL : ((1ULL << k) - 1)) << start_bit;

    // Clear bits
    LOCK &= ~mask;

    // TODO: memset maybe
}

void teardown_arena(void) {
    if (!BUF) {
        return; // already torn down or never initialized
    }

#ifdef ARENA_DEBUG
    // Optional: poison memory to catch use-after-free bugs
    memset(BUF, 0xDD, BUF_SIZE * BUF_NUM);
#endif

    free(BUF);
    BUF = NULL;
    LOCK = 0;
}
