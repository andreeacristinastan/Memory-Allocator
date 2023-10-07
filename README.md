#Copyright Stan Andreea-Cristina 333CA

void *os_malloc(size_t size) => This function check if we should use mmap for allocating memory or
                                sbrk().
                                If we use sbrk(), we check if it's the first node from our, if yes we check
                                call request space function and just alloc MMAP_THRESHOLD - STRUCT_SIZE
                                dimmension for spliting it later.
                                If not, we check if we can coalesce blocks that are free consecutive and
                                after we try to find a node in list which can store the block that e want
                                to malloc.
                                If we don't find a proper block, just try to expand the last one and after
                                alloc new memory at the end of the list. If we find a free block, we're trying
                                to split it to make space for another possible allocation and after that return
                                the pointer to the region after structure.

void *os_coalesce_realloc(struct block_meta *block, size_t size) => This function is used to coalesce blocks right
                                after the one that we want to realloc if they have status free, and at the same time 
                                trying to find if will fit the new size of our block.
                                If we are at the end of the list (even the last node doesn't have a status free), means
                                that we need another node with another zone of memory.
                                If we have the last node in the list status free, we expand it as much as we need.

void *os_realloc(void *ptr, size_t size) => Function that will realloc the current block.
                                After checking all the corner cases, we have three cases:
                                -When the block is status mapped / size >= MMAPTHERSHOLD and then we 
                                have to malloc another zone and copy the content of the block in it as
                                much as we can (if the new size could not accomodate the previous data, we wil copy just
                                the maxmum that we can) and free the previous ptr.
                                If the size of the block is bigger than the new size it meand that we just check if we
                                can split the node in chunks for make space for another possible block.
                                If the size of the block is smaller than the new size we will call the coalesce function
                                and we will try to get the perfect block for realloc what we need.
