#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <x86intrin.h>
#include <malloc.h>
#include <stdint.h>
#include <time.h>
#include "B+Tree.h"

#define mfence() asm volatile("mfence":::"memory")

unsigned long IN_count = 0;
unsigned long LN_count = 0;

void flush_buffer_nocount(void *buf, unsigned long len, bool fence)
{
	unsigned long i;
	len = len + ((unsigned long)(buf) & (CACHE_LINE_SIZE - 1));
	if (fence) {
		mfence();
		for (i = 0; i < len; i += CACHE_LINE_SIZE)
			asm volatile ("clflush %0\n" : "+m" (*(char *)(buf+i)));
		mfence();
	} else {
		for (i = 0; i < len; i += CACHE_LINE_SIZE)
			asm volatile ("clflush %0\n" : "+m" (*(char *)(buf+i)));
	}
}

LN *allocLNode()
{
	LN *node = pmalloc(sizeof(LN));
	node->type = THIS_LN;
	node->nKeys = 0;
	LN_count++;
	return node;
}

IN *allocINode()
{
	IN *node = malloc(sizeof(IN));
	node->type = THIS_IN;
	node->nKeys = 0;
	IN_count++;
	return node;
}

tree *initTree()
{
	tree *t =malloc(sizeof(tree)); 
	t->root = allocLNode();
	((LN *)t->root)->pNext = NULL;
	return t;
}

void *Lookup(tree *t, unsigned long key)
{
	int loc = 0;
	void *value = NULL;
	LN *curr = t->root;
	curr = find_leaf_node(curr, key);

	loc = Search((IN *)curr, key);

	if (curr->keys[loc] == key)
		value = curr->ptr[loc];
	return value;
}

void Range_Lookup(tree *t, unsigned long start_key, unsigned int num, 
		unsigned long buf[])
{
	unsigned long i, search_count = 0;
	LN *curr = t->root;
	curr = find_leaf_node(curr, start_key);

	while (curr != NULL) {
		for (i = 0; i < curr->nKeys; i++) {
			buf[search_count] = *(unsigned long *)curr->ptr[i];
			search_count++;
			if (search_count == num)
				return ;
		}
		curr = curr->pNext;
	}
}

int Search(IN *curr, unsigned long key)
{
	int low = 0, mid = 0;
	int high = curr->nKeys - 1;

	while (low <= high){
		mid = (low + high) / 2;
		if (curr->keys[mid] > key)
			high = mid - 1;
		else if (curr->keys[mid] < key)
			low = mid + 1;
		else
			break;
	}

	if (low > mid) 
		mid = low;

	return mid;
}

void *find_leaf_node(void *curr, unsigned long key) 
{
	unsigned long loc;

	if (((LN *)curr)->type == THIS_LN) 
		return curr;
	loc = Search(curr, key);

	if (loc > ((IN *)curr)->nKeys - 1) 
		return find_leaf_node(((IN *)curr)->ptr[loc - 1], key);
	else if (((IN *)curr)->keys[loc] <= key) 
		return find_leaf_node(((IN *)curr)->ptr[loc], key);
	else if (loc == 0) 
		return find_leaf_node(((IN *)curr)->leftmostPtr, key);
	else 
		return find_leaf_node(((IN *)curr)->ptr[loc - 1], key);
}

void Insert(tree *t, unsigned long key, void *value)
{
	LN *curr = t->root;
	/* Find proper leaf */
	curr = find_leaf_node(curr, key);

	/* Check overflow & split */
	if (curr->nKeys < NUM_LN_ENTRY) {
		insert_in_leaf(curr, key, value);
	} else {
		int i, j, loc, curr_nKeys;
		LN *split_LNode = allocLNode();
		curr_nKeys = curr->nKeys;

		for (j = MIN_LN_ENTRIES, i = 0; j < curr_nKeys; j++, i++) {
			split_LNode->keys[i] = curr->keys[j];
			split_LNode->ptr[i] = curr->ptr[j];
			split_LNode->nKeys++;
			curr->nKeys--;
		}

		if (split_LNode->keys[0] > key)
			insert_in_leaf(curr, key, value);
		else
			insert_in_leaf(split_LNode, key, value);

		insert_in_parent(t, curr, split_LNode->keys[0], split_LNode);

		split_LNode->pNext = curr->pNext;
		curr->pNext = split_LNode;
	}
}

void insert_in_leaf(LN *curr, unsigned long key, void *value)
{
	int loc, mid, j;

	mid = Search((IN *)curr, key);

	for (j = (curr->nKeys - 1); j >= mid; j--) {
		curr->keys[j + 1] = curr->keys[j];
		curr->ptr[j + 1] = curr->ptr[j];
	}

	curr->keys[mid] = key;
	curr->ptr[mid] = value;

	curr->nKeys++;
}

void insert_in_inner(IN *curr, unsigned long key, void *child)
{
	int loc, mid, j;

	mid = Search(curr, key);

	for (j = (curr->nKeys - 1); j >= mid; j--) {
		curr->keys[j + 1] = curr->keys[j];
		curr->ptr[j + 1] = curr->ptr[j];
	}

	curr->keys[mid] = key;
	curr->ptr[mid] = child;

	curr->nKeys++;
}

void insert_in_parent(tree *t, void *curr, unsigned long key, void *splitNode) {
	if (curr == t->root) {
		IN *root = allocINode();
		root->leftmostPtr = curr;
		root->keys[0] = key;
		root->ptr[0] = splitNode;
		root->nKeys++;

		((IN *)splitNode)->parent = root;
		((IN *)curr)->parent = root;
		t->root = root;
		return ;
	}

	IN *parent;

	if (((IN *)curr)->type == THIS_IN)
		parent = ((IN *)curr)->parent;
	else
		parent = ((LN *)curr)->parent;

	if (parent->nKeys < NUM_IN_ENTRY) {
		insert_in_inner(parent, key, splitNode);
		((IN *)splitNode)->parent = parent;
	} else {
		int i, j, loc, parent_nKeys;
		IN *split_INode = allocINode();
		parent_nKeys = parent->nKeys;

		for (j = MIN_IN_ENTRIES, i = 0; j < parent_nKeys; j++, i++) {
			split_INode->keys[i] = parent->keys[j];
			split_INode->ptr[i] = parent->ptr[j];
			((IN *)split_INode->ptr[i])->parent = split_INode;
			split_INode->nKeys++;
			parent->nKeys--;
		}

		if (split_INode->keys[0] > key) {
			insert_in_inner(parent, key, splitNode);
			((IN *)splitNode)->parent = parent;
		}
		else {
			((IN *)splitNode)->parent = split_INode;
			insert_in_inner(split_INode, key, splitNode);
		}

		insert_in_parent(t, parent, split_INode->keys[0], split_INode);
	}
}

/*
void *Update(tree *t, unsigned long key, void *value)
{
	unsigned long loc = 0;
	LN *curr = t->root;
	curr = find_leaf_node(curr, key);

	while (loc < NUM_LN_ENTRY) {
		loc = find_next_bit(&curr->bitmap, BITMAP_SIZE, loc);
		if (loc == BITMAP_SIZE)
			break;
		
		if (curr->fingerprints[loc] == hash(key) &&
				curr->entries[loc].key == key) {
			curr->entries[loc].ptr = value;
			flush_buffer(&curr->entries[loc].ptr, 8, true);
			return curr->entries[loc].ptr;
		}
		loc++;
	}

	return NULL;
}
*/

/*
int delete_in_leaf(node *curr, unsigned long key)
{
	int mid, j;

	curr->bitmap = curr->bitmap - 1;
	flush_buffer(&curr->bitmap, sizeof(unsigned long), true);

	mid = Search(curr, curr->slot, key);

	for (j = curr->slot[0]; j > mid; j--)
		curr->slot[j - 1] = curr->slot[j];

	curr->slot[0] = curr->slot[0] - 1;

	flush_buffer(curr->slot, sizeof(curr->slot), true);
	
	curr->bitmap = curr->bitmap + 1 - (0x1UL << (mid + 1));
	flush_buffer(&curr->bitmap, sizeof(unsigned long), true);
	return 0;
}

int Delete(tree *t, unsigned long key)
{
	int numEntries, errval = 0;
	node *curr = t->root;
	
	curr = find_leaf_node(curr, key);

	numEntries = curr->slot[0];
	if (numEntries <= 1)
		errval = -1;
	else
		errval = delete_in_leaf(curr, key);

	return errval;
}
*/
