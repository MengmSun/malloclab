#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*Private glovbal variables*/
static char* heap_listp = NULL;
static char* root = NULL;
static char* block_list_start = NULL;
static void* extend_heap(size_t size);
static void* coalesce(void* bp);
static void* find_fit(size_t size);
static void place(void* bp, size_t size);
static void insert(char* bp);
static void delete(char* bp);
static char* find_list_root(size_t size);

team_t team = {
    /* Team name */
    "lxs",
    /* First member's full name */
    "Xiaosu",
    /* First member's email address */
    "lxs@XXX.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* Basic constants and macros */
#define WSIZE 4     /* Word and header/footer size (bytes) */
#define DSIZE 8     /* Double word size (bytes) */
#define CHUNKSIZE (1<<12)     /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into  word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute prev and next free block ptr*/
#define DOWN_FREE_BLKP(bp) ((char*)(bp))
#define ABOV_FREE_BLKP(bp) ((char*)(bp+WSIZE))

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* DEBUG */
#define DEBUG 0

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*
 * mm_init - initialize the malloc package.
 * The return value should be -1 if there was a problem in performing the initialization, 0 otherwise
 */
int mm_init(void)
{
    if((heap_listp = mem_sbrk(14*WSIZE))==(void *)-1){
        return -1;
    }
    PUT(heap_listp, 0);
    PUT(heap_listp+(1*WSIZE), 0);
    PUT(heap_listp+(2*WSIZE), 0);
    PUT(heap_listp+(3*WSIZE), 0);
    PUT(heap_listp+(4*WSIZE), 0);
    PUT(heap_listp+(5*WSIZE), 0);
    PUT(heap_listp+(6*WSIZE), 0);
    PUT(heap_listp+(7*WSIZE), 0);
    PUT(heap_listp+(8*WSIZE), 0);
    PUT(heap_listp+(9*WSIZE), 0);
    PUT(heap_listp+(10*WSIZE), 0);                          //why would this block exist?
    PUT(heap_listp+(11*WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp+(12*WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp+(13*WSIZE), PACK(0, 1));

    block_list_start = heap_listp;
    heap_listp += (12*WSIZE);

    /*explict block's size is aligned to 8 bytes*/
    if(extend_heap(CHUNKSIZE/DSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * find_list_root - find the exact list according to the block size
 * */
static char* find_list_root(size_t size)
{
    int index = 0;
    if(size <= 8) index = 0;
    else if(size <= 16) index = 1;
    else if(size <= 32) index = 2;
    else if(size <= 64) index = 3;
    else if(size <= 128) index = 4;
    else if(size <= 256) index = 5;
    else if(size <= 512) index = 6;
    else if(size <= 1024) index = 7;
    else if(size <= 2048) index = 8;
    else index = 9;
    return block_list_start + index*WSIZE;
}

/*
 * insert - insert free block into the list
 * expand from small to big
 * */
static void insert(char* bp)
{
    char* root = find_list_root(GET_SIZE(HDRP(bp)));
    char* down = GET(root);
    char* abov = root;

    while(down != NULL) {
        if(GET_SIZE(HDRP(down)) >= GET_SIZE(HDRP(bp)))
            break;
        abov = down;
        down = GET(DOWN_FREE_BLKP(down));
    }
    if(abov == root) {
        PUT(DOWN_FREE_BLKP(bp), down);
        PUT(ABOV_FREE_BLKP(bp), NULL);
        if(down != NULL)
            PUT(ABOV_FREE_BLKP(down), bp);
        PUT(root, bp);
    }
    else {
        PUT(DOWN_FREE_BLKP(abov), bp);
        PUT(ABOV_FREE_BLKP(bp), abov);
        PUT(DOWN_FREE_BLKP(bp), down);
        if(down != NULL)
            PUT(ABOV_FREE_BLKP(down), bp);
    }
}

/*
 * delete - delete block bp from the list
 * bp is still a free block,just coalesced with others
 * */
static void delete(char* bp)
{
    char* root = find_list_root(GET_SIZE(HDRP(bp)));
    char* abov = GET(ABOV_FREE_BLKP(bp));
    char* down = GET(DOWN_FREE_BLKP(bp));

    if(abov == NULL) {
        if(down != NULL)
            PUT(ABOV_FREE_BLKP(down), 0);
        PUT(root, down);
    }
    else {
        if(down != NULL)
            PUT(ABOV_FREE_BLKP(down), abov);
        PUT(DOWN_FREE_BLKP(abov), down);
    }

    PUT(DOWN_FREE_BLKP(bp), 0);
    PUT(ABOV_FREE_BLKP(bp), 0);
}

/*
 * coalesce
 * */
static void* coalesce(void* bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if(prev_alloc && next_alloc) {
    }
    else if(prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        delete(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));               //right?
    }
    else if(!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        delete(PREV_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        delete(PREV_BLKP(bp));
        delete(NEXT_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert(bp);
    return bp;
}

/*
 * extend_heap
 * */
static void* extend_heap(size_t words)
{
    char* bp;
    size_t size;

    /*allocate an even number to maintain alignment*/
    size = (words%2)?(words+1)*DSIZE:words*DSIZE;
    if((long)(bp = mem_sbrk(size)) ==(void*) -1)
        return NULL;

    /*Initialize free block header/footer and the epilogue header*/
    PUT(HDRP(bp), PACK(size, 0));                /*Free block header*/
    PUT(FTRP(bp), PACK(size, 0));                /*Free block footer*/
    PUT(DOWN_FREE_BLKP(bp), 0);                  /*Previous free block*/
    PUT(ABOV_FREE_BLKP(bp), 0);                  /*Next free block*/
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1));         /*New epilogue header*/

    return coalesce(bp);
}

/*
 * find_fit - First Fit strategy
 * - explicit free list
 * */
static void* find_fit(size_t size)
{
    /*First fit search*/
    char* root = find_list_root(size);
    char* cur;

    for(root;root != heap_listp-2*WSIZE;root += WSIZE) {
        cur = (char*)GET(root);
        while(cur != NULL) {
            if(GET_SIZE(HDRP(cur)) >= size)
                return cur;
            cur = GET(DOWN_FREE_BLKP(cur));
        }
    }
    return NULL;
}

/*
 * place
 * */
static void place(void* bp, size_t size)
{
    size_t csize = GET_SIZE(HDRP(bp));

    /*block bp must be deleted anyway*/
    delete(bp);
    /*6WSIZE = header+footer+down+abov+block*/
    if((csize-size) >= 6*WSIZE) {
        PUT(HDRP(bp), PACK(size, 1));
        PUT(FTRP(bp), PACK(size, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-size, 0));
        PUT(FTRP(bp), PACK(csize-size, 0));     /*whether should we return the older bp or not?*/
        PUT(DOWN_FREE_BLKP(bp), 0);
        PUT(ABOV_FREE_BLKP(bp), 0);
        coalesce(bp);
    }
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;                               /*Ajusted block size*/
    size_t extendsize;                          /*Amount to extend heap if no fit*/
    char* bp;

    /*Ignore spurious request*/
    if(size == 0)
        return NULL;

    /*Ajust block size to include overhead and alignment reqs*/
    if(size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE*((size + DSIZE + (DSIZE-1))/DSIZE);

    /*Seach the free list for a fit*/
    if((bp = find_fit(asize)) != NULL) {
        place(bp,asize);
        return bp;
    }

    /*No fit found.Get more memory and place the block*/
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize/DSIZE)) == NULL)
        return NULL;
    place(bp,asize);
    return bp;
}

/*
 * mm_free - Freeing a block
 */
void mm_free(void *bp)
{
    if(bp == 0)
        return;
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(DOWN_FREE_BLKP(bp), 0);
    PUT(ABOV_FREE_BLKP(bp), 0);
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    /*
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;

    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
    */

    //ptr is a block pointer
    void* newptr;
    size_t csize;
    size_t newsize;

    //if ptr = NULL,it's equivalent to mm_malloc(size)
    if(ptr == NULL) {
        newptr = mm_malloc(size);
        if(!newptr) {
            return NULL;
        }
        return newptr;
    }

    //if size = 0,it's equivalent to mm_free(ptr)
    if(size == 0) {
        mm_free(ptr);
        return NULL;
    }

    //normal realloc
    csize = GET_SIZE(HDRP(ptr));
    newsize = ALIGN(size) + DSIZE;
    newptr = ptr;
    PUT(HDRP(newptr), PACK(newsize, 1));
    PUT(HDRP(newptr), PACK(newsize, 1));
    return newptr;

}

