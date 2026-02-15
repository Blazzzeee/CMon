#include "arena.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MEMBER_BITS 64

#define MEMBER_INDEX(bit) ((bit) / MEMBER_BITS)
#define BIT_OFFSET(bit) ((bit) % MEMBER_BITS)
#define GLOBAL_BIT(member, bit) ((member) * MEMBER_BITS + (bit))
#define ARENA_BUF_NUM 64
#define ARENA_BUF_SIZE 512

size_t arena_buf_size = ARENA_BUF_SIZE;
size_t arena_buf_num = ARENA_BUF_NUM;
size_t arena_lock_members = 0;

// Means there can only exist one arena at a time, since the state is global
static uint64_t *LOCK;
// TODO: Extend number of chunks , by using bitmask array
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

void arena_config(size_t buf_size, size_t buf_num) {
    arena_buf_size = buf_size;
    arena_buf_num = buf_num;

    arena_lock_members = (arena_buf_num + MEMBER_BITS - 1) / MEMBER_BITS;
}

// Initialises arena for future allocations
void *prealloc_arena() {

    void *tmp;
    size_t total_size = arena_buf_size * arena_buf_num;

    if (!arena_lock_members) {
        arena_lock_members = (arena_buf_num + MEMBER_BITS - 1) / MEMBER_BITS;
    }

    if ((tmp = malloc(sizeof(uint64_t) * arena_lock_members)) == NULL) {
        fprintf(stderr, "prealloc_arena \n");
        return NULL;
    } else {
        LOCK = tmp;
    }

    // Reset lock to zero
    memset(LOCK, 0, sizeof(uint64_t) * arena_lock_members);

    tmp = NULL;
    // Initialise global memory , that will be used for arena allocations
    // tmp = malloc(total_size);
    tmp = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    // TODO: replace with mmap

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
    int start_bit = -1;
    size_t need = req + sizeof(arena_hdr_t);
    uint64_t k = (need + arena_buf_size - 1) / arena_buf_size;

    for (size_t m = 0; m < arena_lock_members; m++) {

        uint64_t free_mask = ~LOCK[m];
        int local_bit = find_k_consecutive_zeroes(free_mask, k);

        /* 🚨 ensure allocation does not cross member boundary */
        if ((size_t)local_bit + k > MEMBER_BITS)
            continue;

        if (local_bit != -1) {
            start_bit = (m * 64) + local_bit;
            break;
        }
    }

    if (start_bit == -1)
        // Try next 64 bit member
        return NULL;

    size_t member = start_bit / 64;
    size_t bit = start_bit % 64;

    uint64_t claim_mask = (k == 64) ? ~0ULL : ((1ULL << k) - 1);

    claim_mask <<= bit;
    // Create a mask of K bits set to 1
    // e.g., if k=3, mask is 0...0111

    // Shift it to the correct position
    // Mark these bits as USED (set to 1) in our global lock
    // Use __atomic_fetch_or for true thread safety if needed

    LOCK[member] |= claim_mask;
    char *base = (char *)BUF + (start_bit * arena_buf_size);
    // Internal 2 byte book keeping structhure enforced in
    // allocations , which helps in deallocation
    arena_hdr_t *hdr = (arena_hdr_t *)base;
    hdr->chunks = (uint16_t)k;

    // Return the pointer: , with arena_hdr_t
    return (void *)(base + sizeof(arena_hdr_t));
}

// O(1) running tine for given K ,
// use bit smear algo on the bitmask
int find_k_consecutive_zeroes(uint64_t free_mask, int k) {

    if (k <= 0)
        return -1;
    uint64_t combined;

    if (free_mask == 0) {
        fprintf(stderr, "FATAL: k_zeroes: Buffer overflow\n");
        return -1;
    }

    combined = free_mask;

    // TODO: In multithreaded context we can replace this with atomic fetch and
    // We shift and AND the mask k-1 times.
    // After k-1 iterations, any bit still set to 1 in 'combined'
    // represents the START of a sequence of k consecutive 1s in free_mask.
    for (int i = 1; i < k; ++i) {
        combined &= (combined >> 1);
    }

    // The free bits are marked as 1
    if (combined == 0) {
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
    if ((char *)hdr < (char *)BUF || (char *)hdr >= (char *)BUF + arena_buf_size * arena_buf_num) {
        fprintf(stderr, "arena_free: invalid pointer\n");
        return;
    }

    // If metadata was corrupted , we cant deallocate
    if (k == 0 || k > arena_buf_num || k > 64) {
        fprintf(stderr, "arena_free: corrupted header (k=%lu)\n", k);
        return;
    }

    // end of allocation fits
    char *end = (char *)hdr + k * arena_buf_size;
    if (end > (char *)BUF + arena_buf_size * arena_buf_num) {
        fprintf(stderr, "arena_free: allocation overruns arena\n");
        return;
    }

    // Compute chunk start
    size_t offset = (char *)hdr - (char *)BUF;
    uint64_t start_bit = offset / arena_buf_size;

    // Build mask
    size_t member = MEMBER_INDEX(start_bit);
    size_t bit = BIT_OFFSET(start_bit);

    if (bit + k > MEMBER_BITS)
        return;

    uint64_t mask = ((1ULL << k) - 1) << bit;

    // Clear bits
    LOCK[member] &= ~mask;
}

void teardown_arena(void) {
    if (!BUF) {
        return; // already torn down or never initialized
    }

#ifdef ARENA_DEBUG
    // Optional: poison memory to catch use-after-free bugs
    memset(BUF, 0xDD, arena_buf_size * arena_buf_num);
#endif

    size_t total_size = arena_buf_size * arena_buf_num;
    munmap(BUF, total_size);
    BUF = NULL;

    if (LOCK) {
        free(LOCK);
        LOCK = NULL;
    }
    arena_lock_members = 0;
}
