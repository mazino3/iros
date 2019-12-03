#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include <kernel/mem/page.h>

#define MALLOC_SCRUB_FREE
#define MALLOC_SCRUB_ALLOC

#define MALLOC_SCRUB_BITS 0xC0
#define FREE_SCRUB_BITS   0xA0

#define __MALLOC_MAGIG_CHECK 0x2A8F30B241BFA759UL

#ifdef __is_libk
#include <kernel/hal/output.h>
#include <kernel/mem/vm_allocator.h>
#include <kernel/mem/vm_region.h>

void *sbrk(intptr_t increment) {
    if (increment < 0) {
        void *res = add_vm_pages_end(0, VM_KERNEL_HEAP);
        remove_vm_pages_end(-increment, VM_KERNEL_HEAP);
        return res;
    }
    return add_vm_pages_end(increment, VM_KERNEL_HEAP);
}
#else
#include <pthread.h>
#include <unistd.h>
#endif

struct metadata {
    size_t magic;
    size_t prev_size;
    size_t size;
} __attribute__((packed));

#define ALLOCATED              (1)
#define SET_ALLOCATED(block)   ((block)->size |= ALLOCATED)
#define CLEAR_ALLOCATED(block) ((block)->size &= ~ALLOCATED)
#define IS_ALLOCATED(block)    ((block)->size & ALLOCATED)
#define GET_SIZE(block)        ((block)->size & ~ALLOCATED)

#define NUM_PAGES_IN_LENGTH(sz) (((sz) -1) / PAGE_SIZE + 1)
#define NEW_BLOCK_SIZE(n)       (2 * sizeof(struct metadata) + (n))
#define NEXT_BLOCK(block)       ((struct metadata *) (((uintptr_t)(block)) + sizeof(struct metadata) + GET_SIZE((block))))
#define PREV_BLOCK(block)       ((struct metadata *) (((uintptr_t)(block)) - sizeof(struct metadata) - (block)->prev_size))
#define GET_BLOCK(p)            ((struct metadata *) (((uintptr_t)(p)) - sizeof(struct metadata)))

static struct metadata *start;
static uintptr_t heap_end;
static struct metadata *last_allocated;

#ifdef __is_libk
#include <kernel/util/spinlock.h>
#define pthread_spin_lock   spin_lock
#define pthread_spin_unlock spin_unlock
#define pthread_spinlock_t  spinlock_t
#else
// FIXME: figure out how this should work: there
//        should not be copying of locking code...
#include <stdatomic.h>

#define CPU_RELAX() asm volatile("pause" : : :);

int pthread_spin_destroy(pthread_spinlock_t *lock) {
    lock->lock = -1;
    return 0;
}

int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
    // FIXME: do something with pshared
    (void) pshared;

    lock->lock = 0;
    return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock) {
    for (;;) {
        int expected = 0;
        if (!atomic_compare_exchange_strong(&lock->lock, &expected, 1)) {
            CPU_RELAX();
            continue;
        }

        break;
    }

    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock) {
    int expected = 0;
    if (!atomic_compare_exchange_strong(&lock->lock, &expected, 1)) {
        // Failed to aquire the lock
        return EBUSY;
    }

    return 0;
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {
    lock->lock = 0;
    return 0;
}
// FIXME: this should be a mutex...
#define SPINLOCK_INITIALIZER \
    { 0 }
#endif /* __is_libk */
static pthread_spinlock_t heap_lock = SPINLOCK_INITIALIZER;

#if defined(__is_libk) && defined(KERNEL_MALLOC_DEBUG)
#undef calloc
void *calloc(size_t n, size_t sz, int line, const char *func) {
    debug_log("Calloc: [ %s, %d ]\n", func, line);
#else
void *calloc(size_t n, size_t sz) {
#endif /* __is_libk && KERNEL_MALLOC_DEBUG */
    void *p = malloc(n * sz);
    if (p == NULL) {
        return p;
    }
    memset(p, 0, n * sz);
    return p;
}

#if defined(__is_libk) && (defined(KERNEL_MALLOC_DEBUG) || defined(KERNEL_MEMCPY_DEBUG))
#include <kernel/hal/output.h>
#undef realloc
void *realloc(void *p, size_t sz, int line, const char *func) {
    debug_log("Realloc: [ %s, %d ]\n", func, line);
#else
void *realloc(void *p, size_t sz) {
#endif /* defined(__is_libk) && (defined(KERNEL_MALLOC_DEBUG) || defined(KERNEL_MEMCPY_DEBUG)) */
    void *new_p = malloc(sz);
    if (p == NULL) {
        return new_p;
    }
    if (new_p == NULL) {
        return new_p;
    }
#if defined(__is_libk) && defined(KERNEL_MEMCPY_DEBUG)
#undef memmove
    memmove(new_p, p, sz, line, func);
#else
    memmove(new_p, p, sz);
#endif /* defined(__is_libk) && defined(KERNEL_MEMCPY_DEBUG) */
    free(p);
    return new_p;
}

#if defined(__is_libk) && defined(KERNEL_MALLOC_DEBUG)
#include <kernel/hal/output.h>
#undef free
void free(void *p, int line, const char *func) {
    debug_log("Free: [ %s, %d, %#.16lX ]\n", func, line, (uintptr_t) p);

    struct metadata *_block = start;
    while (_block != NULL && _block->size != 0) {
        assert(_block->magic == __MALLOC_MAGIG_CHECK);
        _block = NEXT_BLOCK(_block);
    }
#else
void free(void *p) {
#endif /* __is_libk && KERNEL_MALLOC_DEBUG */
    if (p == NULL) {
        return;
    }

    pthread_spin_lock(&heap_lock);

    struct metadata *block = GET_BLOCK(p);
    assert(block->magic == __MALLOC_MAGIG_CHECK);
    assert(IS_ALLOCATED(block));
    CLEAR_ALLOCATED(block);
    last_allocated = NULL;

#ifdef MALLOC_SCRUB_FREE
    memset(block + 1, FREE_SCRUB_BITS, GET_SIZE(block));
#endif /* MALLOC_SCRUB_FREE */

#if defined(KERNEL_MALLOC_DEBUG) && defined(__is_libk)
    debug_log("Malloc block freed: [ %#.16lX ]\n", (uintptr_t) block);
#endif /* KERNEL_MALLOC_DEBUG && __is_libk */

    pthread_spin_unlock(&heap_lock);
}

#if defined(__is_libk) && defined(KERNEL_MALLOC_DEBUG)
#undef aligned_alloc
void *aligned_alloc(size_t alignment, size_t n, int line, const char *func) {
    debug_log("Aligned alloc: [ %s, %d ]\n", func, line);

    struct metadata *_block = start;
    while (_block != NULL && _block->size != 0) {
        assert(_block->magic == __MALLOC_MAGIG_CHECK);
        _block = NEXT_BLOCK(_block);
    }
#else
void *aligned_alloc(size_t alignment, size_t n) {
#endif /* defined(__is_libk) && defined(KERNEL_MALLOC_DEBUG) */
    if (n == 0) {
        return NULL;
    }

    if ((alignment & (alignment - 1)) || (alignment % sizeof(void *) != 0)) {
        errno = EINVAL;
        return NULL;
    }

    n = MAX(n, 16);

    pthread_spin_lock(&heap_lock);

    if (!start) {
        start = sbrk(NUM_PAGES_IN_LENGTH(NEW_BLOCK_SIZE(n)));
        heap_end = ((uintptr_t) start) + NUM_PAGES_IN_LENGTH(NEW_BLOCK_SIZE(n)) * PAGE_SIZE;
        start->prev_size = 0;
        start->size = n;
        start->magic = __MALLOC_MAGIG_CHECK;

        start = NEXT_BLOCK(start);
        start->prev_size = n;
        start->magic = __MALLOC_MAGIG_CHECK;
        start = PREV_BLOCK(start);
    }

    struct metadata *block = start;
    while (block->size != 0) {
        assert(block->magic == __MALLOC_MAGIG_CHECK);
        if (!IS_ALLOCATED(block)) {
            if (((uintptr_t)(block + 1)) % alignment == 0) {
                if (GET_SIZE(block) >= n) {
                    SET_ALLOCATED(block);

                    pthread_spin_unlock(&heap_lock);

#if defined(KERNEL_MALLOC_DEBUG) && defined(__is_libk)
                    debug_log("Malloc block allocated: [ %#.16lX, %d ]\n", (uintptr_t)(block + 1), __LINE__);
#endif /* KERNEL_MALLOC_DEBUG && __is_libk */

#ifdef MALLOC_SCRUB_ALLOC
                    memset(block + 1, MALLOC_SCRUB_BITS, GET_SIZE(block));
#endif /* MALLOC_SCRUB_ALLOC */

                    return block + 1;
                }

                goto aligned_alloc_next;
            }

            // FIXME: attempt to split a large block into smaller ones with alignment
            goto aligned_alloc_next;
        }

    aligned_alloc_next:
        block = NEXT_BLOCK(block);
    }

    struct metadata *new_block =
        (struct metadata *) ((((uintptr_t)(block + 1)) + alignment - (((uintptr_t)(block + 1)) % alignment)) - sizeof(struct metadata));
    assert(((uintptr_t)(new_block + 1)) % alignment == 0);

    if (heap_end <= ((uintptr_t) new_block) + n + sizeof(struct metadata)) {
        sbrk(NUM_PAGES_IN_LENGTH(((uintptr_t) new_block) + n - heap_end) + sizeof(struct metadata));
        heap_end += NUM_PAGES_IN_LENGTH(((uintptr_t) new_block) + n - heap_end + sizeof(struct metadata)) * PAGE_SIZE;
    }

    PREV_BLOCK(block)->size =
        (((uintptr_t) new_block) - ((uintptr_t) PREV_BLOCK(block)) - sizeof(struct metadata)) | (PREV_BLOCK(block)->size & ALLOCATED);
    assert(NEXT_BLOCK(PREV_BLOCK(block)) == new_block);
    new_block->prev_size = PREV_BLOCK(block)->size & ~ALLOCATED;
    new_block->magic = __MALLOC_MAGIG_CHECK;

    new_block->size = n;
    SET_ALLOCATED(new_block);

#ifdef MALLOC_SCRUB_ALLOC
    memset(new_block + 1, MALLOC_SCRUB_BITS, GET_SIZE(new_block));
#endif /* MALLOC_SCRUB_ALLOC */

    struct metadata *tail = NEXT_BLOCK(new_block);
    tail->prev_size = new_block->size;
    tail->size = 0;
    tail->magic = __MALLOC_MAGIG_CHECK;

#if defined(KERNEL_MALLOC_DEBUG) && defined(__is_libk)
    debug_log("Malloc block allocated: [ %#.16lX, %d ]\n", (uintptr_t)(new_block + 1), __LINE__);
#endif /* KERNEL_MALLOC_DEBUG && __is_libk */

    pthread_spin_unlock(&heap_lock);

    return new_block + 1;
}

#if defined(__is_libk) && defined(KERNEL_MALLOC_DEBUG)
#include <kernel/hal/output.h>
#undef malloc
void *malloc(size_t n, int line, const char *func) {
    debug_log("Malloc: [ %s, %d ]\n", func, line);

    struct metadata *_block = start;
    while (_block != NULL && _block->size != 0) {
        assert(_block->magic == __MALLOC_MAGIG_CHECK);
        _block = NEXT_BLOCK(_block);
    }
#else
void *malloc(size_t n) {
#endif /* __is_libk && KERNEL_MALLOC_DEBUG */
    if (n == 0) {
        return NULL;
    }
    if (n % sizeof(uint64_t) != 0) {
        n = n - (n % sizeof(uint64_t)) + sizeof(uint64_t);
    }

    pthread_spin_lock(&heap_lock);

    if (!start) {
        start = sbrk(NUM_PAGES_IN_LENGTH(NEW_BLOCK_SIZE(n)));
        heap_end = ((uintptr_t) start) + NUM_PAGES_IN_LENGTH(NEW_BLOCK_SIZE(n)) * PAGE_SIZE;
        start->prev_size = 0;
        start->size = n;
        SET_ALLOCATED(start);
        start->magic = __MALLOC_MAGIG_CHECK;

        start = NEXT_BLOCK(start);
        start->prev_size = n;
        start->magic = __MALLOC_MAGIG_CHECK;
        start = PREV_BLOCK(start);

        pthread_spin_unlock(&heap_lock);

#if defined(KERNEL_MALLOC_DEBUG) && defined(__is_libk)
        debug_log("Malloc block allocated: [ %#.16lX, %d ]\n", (uintptr_t) start, __LINE__);
#endif /* KERNEL_MALLOC_DEBUG && __is_libk */

#ifdef MALLOC_SCRUB_ALLOC
        memset(start + 1, MALLOC_SCRUB_BITS, GET_SIZE(start));
#endif /* MALLOC_SCRUB_ALLOC */

        last_allocated = start;
        return start + 1;
    }

    struct metadata *block = last_allocated ? last_allocated : start;
    while (block->size != 0) {
        assert(block->magic == __MALLOC_MAGIG_CHECK);
        if (!IS_ALLOCATED(block) && GET_SIZE(block) >= n) {
            SET_ALLOCATED(block);
            last_allocated = block;

            pthread_spin_unlock(&heap_lock);

#if defined(KERNEL_MALLOC_DEBUG) && defined(__is_libk)
            debug_log("Malloc block allocated: [ %#.16lX, %d ]\n", (uintptr_t)(block + 1), __LINE__);
#endif /* KERNEL_MALLOC_DEBUG && __is_libk */

#ifdef MALLOC_SCRUB_ALLOC
            memset(block + 1, MALLOC_SCRUB_BITS, GET_SIZE(block));
#endif /* MALLOC_SCRUB_ALLOC */

            return block + 1;
        }
        block = NEXT_BLOCK(block);
    }

    if (((uintptr_t) block) + NEW_BLOCK_SIZE(n) > heap_end) {
        sbrk(NUM_PAGES_IN_LENGTH(((uintptr_t) block) + NEW_BLOCK_SIZE(n) - heap_end));
        heap_end += NUM_PAGES_IN_LENGTH(((uintptr_t) block) + NEW_BLOCK_SIZE(n) - heap_end) * PAGE_SIZE;
    }

    block->size = n;
    SET_ALLOCATED(block);
    last_allocated = block;
    block = NEXT_BLOCK(block);
    block->prev_size = n;
    block->magic = __MALLOC_MAGIG_CHECK;
    struct metadata *ret = PREV_BLOCK(block) + 1;

#if defined(KERNEL_MALLOC_DEBUG) && defined(__is_libk)
    debug_log("Malloc block allocated: [ %#.16lX, %d ]\n", (uintptr_t)(block + 1), __LINE__);
#endif /* KERNEL_MALLOC_DEBUG && __is_libk */

    pthread_spin_unlock(&heap_lock);

#ifdef MALLOC_SCRUB_ALLOC
    memset(ret, MALLOC_SCRUB_BITS, GET_SIZE(ret - 1));
#endif /* MALLOC_SCRUB_ALLOC */

    return ret;
}