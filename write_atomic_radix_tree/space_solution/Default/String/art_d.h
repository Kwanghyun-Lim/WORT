#include <stdbool.h>

#define META_NODE_SHIFT 8
#define CACHE_LINE_SIZE 64
#define NUM_ENTRY	(0x1UL << META_NODE_SHIFT)

typedef struct Tree tree;
typedef struct Node node;

unsigned long node_count;
unsigned long clflush_count;
unsigned long mfence_count;

struct Node {
	void *entry_ptr[NUM_ENTRY];
};

struct Tree {
	int height;
	node *root;
};

void flush_buffer_nocount(void *buf, unsigned long len, bool fence);
tree *initTree();
int Insert(tree **t, unsigned char *key, int key_len, void *value);
void *Lookup(tree *t, unsigned char *key, int key_len);
