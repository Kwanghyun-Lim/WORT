/* associative array */
void assoc_init(const int hashpower_init);
item *assoc_find(const char *key, const size_t nkey);
int assoc_insert(item *item);
void assoc_delete(const char *key, const size_t nkey);
void do_assoc_move_next_bucket(void);
void stop_assoc_maintenance_thread(void);
extern unsigned int hashpower;
extern unsigned int item_lock_hashpower;


/* W_RADIX_TREE_H */
#include <stdbool.h>
#include <stdint.h>

#define META_NODE_SHIFT		8
#define CACHE_LINE_SIZE 	64
#define NUM_ENTRY			(0x1UL << META_NODE_SHIFT)

typedef struct Tree tree;
typedef struct Node node;

//unsigned long node_count;
//unsigned long mfence_count;
//unsigned long clflush_count;

struct Node {
	void *entry_ptr[NUM_ENTRY];
};

struct Tree {
	int height;
	node *root;
};

tree *initTree(void);
/* flush functions*/
void flush_buffer(void *buf, unsigned long len, bool fence);
