#include "buddy.h"
#include "slub.h"
#include "stdio.h"
#include "vm.h"

struct buddy buddy;

/*
0 +---> 1 +---> 3 +---> 7
  |       |       |
  |       |       +---> 8
  |       |
  |       +---> 4 +---> 9
  |               |
  |               +---> 10
  |
  +---> 2 +---> 5 +---> 11
          |       |
          |       +---> 12
          |
          +---> 6 +---> 13
                  |
                  +---> 14
*/

#define BIN_TREE_UPCHILD(__X) ((__X)*2 + 1)
#define BIN_TREE_DOWNCHILD(__X) ((__X)*2 + 2)
#define BIN_TREE_PARENT(__X) (((__X)-1) / 2)
#define ADDR_TO_INDEX(__A) ((__A))

/**
 * @brief initialize buddy system
 */
void init_buddy_system(void) {
    buddy.pgsize = BUDDY_SPACE_SIZE / PAGE_SIZE;
    buddy.bitmap = kalloc((2 * buddy.pgsize - 1) * sizeof(buddy.bitmap[0]));

    int i = 0;
    for (uint64 layer_size = buddy.pgsize; layer_size; layer_size /= 2)
        for (uint64 node_count = 0; node_count < buddy.pgsize / layer_size; node_count++)
            buddy.bitmap[i++] = layer_size;

    alloc_pages(KERNEL_MAPPING_SIZE);
}

/**
 * @brief allocate space of n pages from low to high
 *
 * @param npages allocated space size in page
 * @return void* virtual addr of the space head (in byte)
 */
void *alloc_pages(int npages) {
    if (buddy.bitmap[0] < npages) {  // No enough space
        panic("Insufficient space in buddy system.");
        return NULL;
    }

    int bm_loc        = 0;
    uint64 alloc_size = buddy.pgsize;
    uint64 ret_val    = BUDDY_START_ADDR;  // Starting from physical 0x0

    while (1) {
        // alloc_size check make sure BIN_TREE_CHILD will not overflow access
        if (alloc_size > 1 && buddy.bitmap[BIN_TREE_UPCHILD(bm_loc)] >= npages)
            bm_loc = BIN_TREE_UPCHILD(bm_loc);
        else if (alloc_size > 1 && buddy.bitmap[BIN_TREE_DOWNCHILD(bm_loc)] >= npages) {
            bm_loc = BIN_TREE_DOWNCHILD(bm_loc);
            ret_val += alloc_size / 2;
        } else
            break;
        alloc_size /= 2;
    }

    for (int i = bm_loc;; i = BIN_TREE_PARENT(i)) {  // sub alloc size from the parent tree
        buddy.bitmap[i] -= alloc_size;
        if (!i)
            break;
    }

    return (void *)(ret_val * PAGE_SIZE);

    /**
     * @brief release the space starting from addr
     *
     * @param addr sapce head of the space to be freed (in byte)
     */
    void free_pages(void *addr) {
        if (addr < 0 || addr >= buddy.pgsize) {
            panic("Invalid free address.");
            return;
        }

        uint64 alloc_size    = 1;
        _Bool size_not_found = 1;

        for (int bm_loc = buddy.pgsize - 1 + PAGE_FLOOR(addr - BUDDY_START_ADDR);;
             bm_loc     = BIN_TREE_PARENT(bm_loc)) {
            if (buddy.bitmap[bm_loc] && size_not_found) {
                alloc_size <<= 1;
            } else {  // Found alloc page
                size_not_found = 0;
                buddy.bitmap[bm_loc] += alloc_size;
                if (!bm_loc)  // Stop at root
                    return;
            }
        }
    }

    /*
    6 +---> 4 +---> 2 +---> 1
      |       |       |
      |       |       +---> 1
      |       |
      |       +---> 2 +---> 1
      |               |
      |               +---> 1
      |
      +---> 2 +---> 0 +---> 1
              |       |
              |       +---> 1
              |
              +---> 2 +---> 1
                      |
                      +---> 1
    */