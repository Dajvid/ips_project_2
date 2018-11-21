// xsedla1d, xhanak34
/**
 * Implementace My MALloc
 * Demonstracni priklad pro 1. ukol IPS/2018
 * Ales Smrcka
 */

#include "mmal.h"
#include <sys/mman.h> // mmap
#include <stdbool.h> // bool
#include <assert.h> // assert
#include <stdio.h>
#include <stdlib.h>

#ifdef NDEBUG
/**
 * The structure header encapsulates data of a single memory block.
 *   ---+------+----------------------------+---
 *      |Header|DDD not_free DDDDD...free...|
 *   ---+------+-----------------+----------+---
 *             |-- Header.asize -|
 *             |-- Header.size -------------|
 */
typedef struct header Header;
struct header {

    /**
     * Pointer to the next header. Cyclic list. If there is no other block,
     * points to itself.
     */
    Header *next;

    /// size of the block
    size_t size;

    /**
     * Size of block in bytes allocated for program. asize=0 means the block
     * is not used by a program.
     */
    size_t asize;
};

/**
 * The arena structure.
 *   /--- arena metadata
 *   |     /---- header of the first block
 *   v     v
 *   +-----+------+-----------------------------+
 *   |Arena|Header|.............................|
 *   +-----+------+-----------------------------+
 *
 *   |--------------- Arena.size ---------------|
 */
typedef struct arena Arena;
struct arena {

    /**
     * Pointer to the next arena. Single-linked list.
     */
    Arena *next;

    /// Arena size.
    size_t size;
};

#define PAGE_SIZE (128*1024)

#endif // NDEBUG

Arena *first_arena = NULL;

/**
 * Return size alligned to PAGE_SIZE
 */
static
size_t allign_page(size_t size)
{
    return size = (((size - 1) / PAGE_SIZE) + 1) * PAGE_SIZE;
}

/**
 * Allocate a new arena using mmap.
 * @param req_size requested size in bytes. Should be alligned to PAGE_SIZE.
 * @return pointer to a new arena, if successfull. NULL if error.
 * @pre req_size > sizeof(Arena) + sizeof(Header)
 */

/**
 *   +-----+------------------------------------+
 *   |Arena|....................................|
 *   +-----+------------------------------------+
 *
 *   |--------------- Arena.size ---------------|
 */
static
Arena *arena_alloc(size_t req_size)
{
    assert(req_size > sizeof(Arena) + sizeof(Header));
    Arena *ret = mmap(NULL, req_size, PROT_READ|PROT_WRITE, MAP_PRIVATE | 0x20, 0, 0);
    if (!ret) {
        return NULL;
    }

    ret->next = NULL;
    ret->size = req_size;
    return ret;
}

/**
 * Appends a new arena to the end of the arena list.
 * @param a     already allocated arena
 */
static
void arena_append(Arena *a)
{
    if (a) { //arena pointer is not null
        if (first_arena) {
            Arena *tmp = first_arena;
            while (tmp->next != NULL) {
                tmp = tmp->next;
            }
            tmp->next = a;
            a->next = NULL;
        } else { //new arena to be first arena
            first_arena = a;
            a->next = NULL;
            Header *first_header = (Header *)(&first_arena[1]);
            first_header->next = first_header;
        }
    }
}

/**
 * Header structure constructor (alone, not used block).
 * @param hdr       pointer to block metadata.
 * @param size      size of free block
 * @pre size > 0
 */
/**
 *   +-----+------+------------------------+----+
 *   | ... |Header|........................| ...|
 *   +-----+------+------------------------+----+
 *
 *                |-- Header.size ---------|
 */
static
void hdr_ctor(Header *hdr, size_t size)
{
    assert(size > 0);
    hdr->size = size;
    hdr->asize = 0;

    Header *first = (Header *)(&first_arena[1]);
    Header *iter = first;
}

/**
 * Checks if the given free block should be split in two separate blocks.
 * @param hdr       header of the free block
 * @param size      requested size of data
 * @return true if the block should be split
 * @pre hdr->asize == 0
 * @pre size > 0
 */
static
bool hdr_should_split(Header *hdr, size_t size)
{
    assert(hdr->asize == 0);
    assert(size > 0);
    return (hdr->size - hdr->asize >= size + sizeof(Header));
}

/**
 * Splits one block in two.
 * @param hdr       pointer to header of the big block
 * @param req_size  requested size of data in the (left) block.
 * @return pointer to the new (right) block header.
 * @pre   (hdr->size >= req_size + 2*sizeof(Header))
 */
/**
 * Before:        |---- hdr->size ---------|
 *
 *    -----+------+------------------------+----
 *         |Header|........................|
 *    -----+------+------------------------+----
 *            \----hdr->next---------------^
 */
/**
 * After:         |- req_size -|
 *
 *    -----+------+------------+------+----+----
 *     ... |Header|............|Header|....|
 *    -----+------+------------+------+----+----
 *             \---next--------^  \--next--^
 */
static
Header *hdr_split(Header *hdr, size_t req_size)
{
    assert((hdr->size >= req_size + 2*sizeof(Header)));
    size_t original_size = hdr->size;
    Header *original_ref = hdr->next;

    hdr->size = req_size;
    Header *new = (Header *)((char *)(&hdr[1]) + hdr->size);

    new->size = original_size - req_size - sizeof(Header);
    new->asize = 0;

    new->next = original_ref;
    hdr->next = new;
    return new;
}

/**
 * Detect if two adjacent blocks could be merged.
 * @param left      left block
 * @param right     right block
 * @return true if two block are free and adjacent in the same arena.
 * @pre left->next == right
 * @pre left != right
 */
static
bool hdr_can_merge(Header *left, Header *right)
{
    assert(left->next == right);
    assert(left != right);

    return (right == (Header *)((char *)&left[1] + left->size));
}

/**
 * Merge two adjacent free blocks.
 * @param left      left block
 * @param right     right block
 * @pre left->next == right
 * @pre left != right
 */
static
void hdr_merge(Header *left, Header *right)
{
    assert(left->next == right);
    assert(left != right);
    left->size += sizeof(Header) + right->size;
    left->next = right->next;
}

/**
 * Finds the first free block that fits to the requested size.
 * @param size      requested size
 * @return pointer to the header of the block or NULL if no block is available.
 * @pre size > 0
 */
static
Header *first_fit(size_t size)
{
    assert(size > 0);
    if (!first_arena) {
        return NULL;
    }

    Header *first = (Header *)(&first_arena[1]);
    Header *current = first->next;

    if ((first->size - first->asize) >= size) {
            return first;
        }

    while (current != first) {
        if ((current->size - current->asize) >= size) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/**
 * Search the header which is the predecessor to the hdr. Note that if
 * @param hdr       successor of the search header
 * @return pointer to predecessor, hdr if there is just one header.
 * @pre first_arena != NULL
 * @post predecessor->next == hdr
 */
static
Header *hdr_get_prev(Header *hdr)
{
    assert(first_arena != NULL);
    Header *anchor = hdr;
    while (hdr->next != anchor) {
        hdr = hdr->next;
    }
    return hdr;
}

/**
 * Allocate memory. Use first-fit search of available block.
 * @param size      requested size for program
 * @return pointer to allocated data or NULL if error or size = 0.
 */
void *mmalloc(size_t size)
{
    Header *found = first_fit(size);
    Arena *new_arena = NULL;
    size_t alligned_size = allign_page(size + sizeof(Header) + sizeof(Arena));
    if (!found) {
        /* get new arena */
        new_arena = arena_alloc(alligned_size);
        if (!new_arena) {
            return NULL;
        }

        new_arena->size = alligned_size;
        arena_append(new_arena);

        /* skip metadata of arena and get pointer to Header */
        found = (Header *)(&new_arena[1]);
        /* connect blocks */
        Header *first = (Header *)(&first_arena[1]);
        Header *last = hdr_get_prev(first);
        found->next = first;
        last->next = found;
        hdr_ctor(found, alligned_size - sizeof(Header) - sizeof(Arena));
    }

    if (hdr_should_split(found, size)) {
        hdr_split(found, size);
    }

    found->asize = size;

    return &found[1];
}

/**
 * Free memory block.
 * @param ptr       pointer to previously allocated data
 * @pre ptr != NULL
 */
void mfree(void *ptr)
{
    assert(ptr != NULL);

    Header *to_free = &((Header *)ptr)[-1];
    to_free->asize = 0;
    Header *prev = hdr_get_prev(to_free);

    if (to_free->next != to_free) {
        if (to_free->asize == 0 && to_free->next->asize == 0 && hdr_can_merge(to_free, to_free->next)) {
            hdr_merge(to_free, to_free->next);
        }
        if (prev->asize == 0 && to_free->asize == 0 && hdr_can_merge(prev, to_free)) {
            hdr_merge(prev, to_free);
        }
    }
}

/**
 * Reallocate previously allocated block.
 * @param ptr       pointer to previously allocated data
 * @param size      a new requested size. Size can be greater, equal, or less
 * then size of previously allocated block.
 * @return pointer to reallocated space or NULL if size equals to 0.
 */
void *mrealloc(void *ptr, size_t size)
{
    Header *to_realloc = &((Header *)ptr)[-1];
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    if (hdr_can_merge(to_realloc, to_realloc->next) && (size >= to_realloc->size + to_realloc->next->size)) {
        hdr_merge(to_realloc, to_realloc->next);
    } else {
        char *new = mmalloc(size);
        if (!new) {
            return NULL;
        }

        // for(unsigned int i = 0; i < to_realloc->asize; i++) {
        //     new[i] = *((char *)&to_realloc[i]);
        // }

        return &(((Header *)new)[1]);
    }

    return &to_realloc[1];
}
