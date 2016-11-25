#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

/*Private glovbal variables*/
static char* mem_heap;          /*Points to first byte of heap*/
static char* mem_brk;           /*Points to last byte of heap plus 1*/
static char* mem_max_addr;      /*Max legal heap addr plus 1*/

/*Basic constants and macros*/
#define WSIZE 4          /*Wrod and header/footer size in bytes*/
#define DSIZE 8          /*Double word size in bytes*/
#define CHUNKSIZE (1<<12)/*Extend heap by this amount in bytes-4KB*/

#define MAX(a,b) ((a)>(b)?(a):(b))

/*Pack a size and allocated bit into a word*/
#define PACK(size,alloc) ((size)|(alloc))

/*Read and write a word at address p*/
#define GET(p) (*(unsigned int*)(p) )
#define PUT(p, val) (*(unsigned int*)(p) = (val) )

/*Read the size and allocated fields from address p*/
#define GET_SIZE(p) (GET(p) & (~0x7) )
#define GET_ALLOC(p) (GET(p) & (0x1) )

/*Given block ptr bp,compute its address of its header and footer*/
#define HDRP(bp) ((char*)(bp) - WSIZE))
#define FTRp(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/*Given block ptr bp,compute address of next and previous blocks*/
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(GET((char*)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(GET((char*)(bp)-DSIZE)))

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*
 * mem_sbrk - Simple model of the sbrk function.Extends the heap
 * by incr bytes and return the start address of the new area.In
 * this model,the heap cannot be shrunk.
 * */

void* mem_sbrk(int incr)
{
    char* old_brk = mem_brk;
    if((incr < 0) || ((mem_brk+incr) > mem_max_addr)) {
        //errno = ENOMEM;
        //fprintf(stderr,"ERROR: mem_sbrk failed.Ran out of memory...\n");
        return (void*)-1;
    }
    mem_brk += incr;
    return (void*)old_brk;
}

/*
 *coalesce
 * */

static void* coalesce(void* bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if(prev_alloc && next_alloc) {
        return bp;
    }
    else if(prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));               //right?
    }
    else if(!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}

/*
 * extend_heap
 * */
static void* extend_heap(size_t words)
{
    char bp;
    size_t size;

    /*allocate an even number to maintain alignment*/
    size = (words%2)?(words+1)*WSIZE:words*WSIZE;
    if((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /*Initialize free block header/footer and the epilogue header*/
    PUT(HDRP(bp),PACK(size, 0));                /*Free block header*/
    PUT(FTRP(bp),PACK(size, 0));                /*Free block footer*/
    PUT(HDRP(NEXT_BLKP(bp)),PACK(0,1));         /*New epilogue header*/

    /*Coalesce if the previous block was free*/
    return coalesce(bp);
}
/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /*Create the initial empty heap*/
    if(heap_listp = mem_sbrk(4*WSIZE) == (void*)-1)
        return -1;
    PUT(heap_listp, 0);                         /*Alignment padding*/
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));/*Prologue header*/
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));/*Prologue footer*/
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));    /*Epilogue header*/
    heap_listp += 2*WSIZE;

    /*Extend the empty heap with a free block of CHUNKSIZE bytes*/
    if(extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * find_fit - First Fit strategy
 * - implicit free list
 * */
static char* find_fit(size_t size)
{
    /*First fit search*/
    void* bp;

    for(bp = heap_listp ; GET_SIZE(HDRP(bp)) > 0 ; bp = NEXT_BLKP(bp))
        if(!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) >= size)
            return bp;
    return NULL;
}

/*
 * place
 * */
static void place(char* bp, size_t size)
{
    size_t csize = GET_SIZE(HDRP(bp));
    if((csize-size) >= 2*DSIZE) {
        PUT(HDRP(bp), PACK(size, 1));
        PUT(FTRP(bp), PACK(size, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-size, 0));
        PUT(FTRP(bp), PACK(csize-size, 0));     /*whether should we return the older bp or not?*/
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
    if(bp = find_fit(asize) != NULL) {
        place(bp,asize);
        return bp;
    }

    /*No fit found.Get more memory and place the block*/
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp,asize);
    return bp;
}

/*
 * mm_free - Freeing a block
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
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
}
