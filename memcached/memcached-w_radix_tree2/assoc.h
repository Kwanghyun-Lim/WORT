/* associative array */
void assoc_init(const int hashpower_init);
item *assoc_find(const char *key, const size_t nkey, const uint32_t hv);
int assoc_insert(item *item, const uint32_t hv);
void assoc_delete(const char *key, const size_t nkey, const uint32_t hv);
void do_assoc_move_next_bucket(void);
void stop_assoc_maintenance_thread(void);
extern unsigned int hashpower;
extern unsigned int item_lock_hashpower;


/* W_RADIX_TREE_H */
#include <stdbool.h>

#define META_NODE_SHIFT 3
#define CACHE_LINE_SIZE 64

typedef struct Tree tree;
typedef struct Node node;
typedef struct Logentry logentry;

struct Logentry { 
	unsigned long addr;
	unsigned long old_value;
	unsigned long new_value;
};

struct Node {  
	void *entry_ptr[8];
	node *parent_ptr;
	unsigned int p_index;  
};

struct Tree { 	
	node *root;
	unsigned char height;
};


/* flush functions*/
void flush_buffer(void *buf, unsigned long len, bool fence);

/* structure allocation functions */
node *allocNode(node *parent, unsigned int index); 
tree *initTree(void); 

/* [INSERT] linking functions */
int increase_radix_tree_height(tree **t, unsigned char new_height);
tree *CoW_Tree(node *changed_root, unsigned char height);

int recursive_alloc_nodes(node *temp_node, unsigned long key, item *value,
		unsigned char height);
int recursive_search_leaf(node *level_ptr, unsigned long key, item *value,
		unsigned char height);
int Insert(tree **t, unsigned long key, item *value);

/* [SEARCH] searching functions */
void *Lookup(tree *t, unsigned long key);

