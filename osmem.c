// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "helpers.h"

#define MMAP_THRESHOLD	(128 * 1024)
#define MMAP_THRESHOLD_CALLOC 4096
#define ALIGNMENT 8 // must be a power of 2
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define STRUCT_SIZE sizeof(struct block_meta)

void *global_base;

/**
 * @brief - This function is used to check if it's a free space
 *			in the list to store the data that we want to alloc
 *			instead of creating a new node with a different zone of
 *			memory.
			We will choose the one with the best suitable size for
			dimension that we want to store.
 * @param last - pointer to check if we have or not free space
 *				(if not this will point to the last position from
 *				the list)
 *
 * @param size - dimension of our block
 * @return struct block_meta* - pointer to the best fit node
 */
struct block_meta *find_free_block(struct block_meta **last, size_t size)
{
	struct block_meta *current = global_base, *result = NULL;
	size_t min_size = INT_MAX;

	while (current) {
		if (current->status == STATUS_FREE && ALIGN(current->size) >= ALIGN(size) && min_size > ALIGN(current->size)) {
			min_size = ALIGN(current->size);
			result = current;
			result->size = ALIGN(current->size);
		} else {
			*last = current;
		}
		current = current->next;
	}
	return result;
}

/**
 * @brief -If we won't find a free block in the list to
 *			store the block, we have to ask the operating
 *			system for space using sbrk and add our new
 *			block to the end of the linked list
 *
 * @param last - pointer to the last node of the list
 * @param size - dimension of our block
 * @return struct block_meta*  - pointer to the node
 */
struct block_meta *request_space(struct block_meta *last, size_t size)
{
	struct block_meta *block;

	block = sbrk(0);
	int status = STATUS_ALLOC;

	if (last) { // NULL on first request.
		last->next = block;
	}

	if (last == NULL) {
		block = sbrk(MMAP_THRESHOLD);
		size = MMAP_THRESHOLD - STRUCT_SIZE;
		status = STATUS_FREE;
	} else if (sbrk(ALIGN(size + STRUCT_SIZE)) == (void *) -1) {
		return NULL; // sbrk failed.
	}

	block->size = size;
	block->next = NULL;
	block->status = status;
	return block;
}

/**
 * @brief - This function implies merging adjacent free blocks
 *			to form a contiguous chunk.
 */
void os_coalesce(void)
{
	struct block_meta *current = global_base;

	while (current->next && current) {
		if (current->status == STATUS_FREE && current->next->status == STATUS_FREE) {
			current->size += current->next->size + STRUCT_SIZE;
			current->next = current->next->next;
		} else {
			current = current->next;
		}
	}
}

/**
 * @brief - This function will help reusing memory blocks
 *			by truncating to the required size and the remaining
 *			bytes would be used to create a new free block.
 *
 * @param block - The block that we want to split
 * @param size - dimension of our block
 */
void os_split(struct block_meta *block, size_t size)
{
	struct block_meta *newblock = (struct block_meta *)((char *)block + STRUCT_SIZE + ALIGN(size));

	newblock->size = block->size - ALIGN(size) - STRUCT_SIZE;

	newblock->status = STATUS_FREE;
	newblock->next = block->next;
	block->size = size;
	block->next = newblock;
}

/**
 * @brief - This function will call mmap to alloc the
 *			memory if the size of our chunck that we want
 *			to alloc is bigger than MMAP_THRESHOLD
 *
 * @param size - dimension of our block
 */
void *os_malloc_mmap(size_t size)
{
	int newSize = ALIGN(size + STRUCT_SIZE);

	void *ptr = mmap(NULL, newSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (ptr) {
		struct block_meta *block = (struct block_meta *)ptr;

		block->size = newSize;
		block->status = STATUS_MAPPED;
		block->next = NULL;
		return (block + 1);
	}

	return NULL;
}

/**
 * @brief - This function is alocating memory after check which
 *			case we have : when size is bigger than MMAP_THRESHOLD
 *			or not, if yes, use mmap, if not use sbrk.
 *
 * @param size - dimension of our block
 */
void *os_malloc(size_t size)
{
	if (size <= 0)
		return NULL;

	if (size >= MMAP_THRESHOLD)
		return os_malloc_mmap(size);

	struct block_meta *block;

	if (!global_base) { // First call.
		block = request_space(NULL, size);
		if (!block)
			return NULL;

		global_base = block;
	}

	struct block_meta *last = global_base;

	if (global_base != NULL)
		os_coalesce();

	block = find_free_block(&last, size);

	if (!block) { // Failed to find free block.
		if (global_base != NULL)
			os_coalesce();

		if (last->status == STATUS_FREE) {
			int newSize = ALIGN(size) - last->size;

			sbrk(newSize);
			last->size = ALIGN(size);
			block = last;
		} else {
			block = request_space(last, size);
			if (!block)
				return NULL;
		}
	} else {
		if (ALIGN(block->size) >= (8 + STRUCT_SIZE) + ALIGN(size))
			os_split(block, size);
	}
	block->status = STATUS_ALLOC;

	return(block+1);
}

/**
 * @brief - This function frees the memory for our blocks
 *			allocated with mmap
 *
 * @param ptr - pointer to what we want to free
 */
void os_free(void *ptr)
{
	if (!ptr)
		return;

	struct block_meta *block = (struct block_meta *)ptr - 1;

	if (block->status == STATUS_MAPPED) {
		munmap(block, block->size);
	} else {
		block->status = STATUS_FREE;
		block->size = ALIGN(block->size);
	}
}

/**
 * @brief - This function is calling malloc after sets
 *			the proper size to alloc and set the memory
 *			allocated to 0 after malloc call.
 *
 * @param nmemb - Number of elements of array
 * @param size - dimension of our block
 * @return void* - return pointer to our allocated memory
 */
void *os_calloc(size_t nmemb, size_t size)
{
	if (!nmemb || !size)
		return NULL;

	size_t callocSize = nmemb * size;
	void *ptr;

	if (ALIGN(callocSize + STRUCT_SIZE) >= MMAP_THRESHOLD_CALLOC)
		ptr = os_malloc_mmap(callocSize);
	else
		ptr = os_malloc(callocSize);

	memset(ptr, 0, callocSize);
	return ptr;
}

/**
 * @brief - This function is used by realloc to find if it's possible
 *			to coalesce some blocks to make a suitable fit for what we
 *			want to resize
 *
 * @param block - The block that we want to realloc
 * @param size - new size of block
 * @return void* - pointer to the perfect zone for the block
 */
void *os_coalesce_realloc(struct block_meta *block, size_t size)
{
	struct block_meta *current = block->next;

	block->size = ALIGN(block->size);
	while (current) {
		if (current->status == STATUS_FREE) {
			block->size += current->size + STRUCT_SIZE;
			current = current->next;
			block->next = current;
		} else {
			break;
		}
		if (block->size >= size)
			return block + 1;
	}
	if (current == NULL) {
		int newSize = ALIGN(size) - block->size;

		sbrk(newSize);
		block->size = ALIGN(size);
		block->status = STATUS_ALLOC;
		return block + 1;
	}
	void *newBlock = os_malloc(size);

	memcpy(newBlock, block + 1, block->size);
	os_free(block + 1);

	return newBlock;
}

/**
 * @brief - This function changes the size of the memory block pointed
 *			to by ptr to size bytes. If the size is smaller than the
 *			previously allocated size, the memory block will be truncated
 *
 * @param ptr - the block on heap
 * @param size - the new size of it
 */
void *os_realloc(void *ptr, size_t size)
{
	struct block_meta *block = (struct block_meta *)ptr - 1;

	if (!ptr)
		return os_malloc(size);

	if (block->status == STATUS_FREE)
		return NULL;

	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	if (block->status == STATUS_MAPPED || size >= MMAP_THRESHOLD) {
		void *newBlock = os_malloc(size);

		memcpy(newBlock, ptr, size < block->size ? size : block->size);
		os_free(ptr);

		return newBlock;
	}

	if (block->size >= size) {
		if (ALIGN(block->size) >= (8 + STRUCT_SIZE) + ALIGN(size))
			os_split(block, size);

		return ptr;
	}

	return os_coalesce_realloc(block, size);
}
