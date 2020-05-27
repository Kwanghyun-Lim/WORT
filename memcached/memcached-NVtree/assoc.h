/* associative array */
void assoc_init(const int hashpower_init);
item *assoc_find(const char *key, const size_t nkey);
int assoc_insert(item *item);
void assoc_delete(const char *key, const size_t nkey);

extern unsigned int hashpower;
extern unsigned int item_lock_hashpower;


#include <stdbool.h>
#include <limits.h>
#define CACHE_LINE_SIZE     64
#define MAX_NUM_ENTRY_IN    509
#define MAX_NUM_ENTRY_PLN   255
#define MAX_NUM_ENTRY_LN    169
#define MAX_KEY             "~"

typedef struct entry entry;
typedef struct Internal_Node IN;
typedef struct Parent_Leaf_Node PLN;
typedef struct Leaf_Node LN;
typedef struct tree tree;
					   
						    
typedef struct {
     int key_len;
	 unsigned char key[];
} key_item;
									 
struct PLN_entry {
     key_item *key;
     LN *ptr;
};
  
struct LN_entry {
     bool flag;
     key_item *key;
     void *value;
};

struct Internal_Node {
	 unsigned int nKeys;
	 key_item *key[MAX_NUM_ENTRY_IN];
	 char dummy[16];
};
 
struct Parent_Leaf_Node {
	  unsigned int nKeys;
	  struct PLN_entry entries[MAX_NUM_ENTRY_PLN];
	  char dummy[8];
};
 
struct Leaf_Node {
     unsigned char nElements;
     LN *sibling;
     unsigned long parent_id;
     struct LN_entry LN_Element[MAX_NUM_ENTRY_LN];
     char dummy[16];
};
 
struct tree{
     unsigned char height;
     unsigned char is_leaf;  // 0.LN 1.PLN 2.IN
     unsigned long first_PLN_id;
     unsigned long last_PLN_id;
     void *root;
     LN *first_leaf;
};


void flush_buffer(void *buf, unsigned long len, bool fence);
void flush_buffer_nocount(void *buf, unsigned long len, bool fence);
key_item *make_key_item(unsigned char *key, int key_len);
void *allocINode(unsigned long num);
LN *allocLNode(void);
int binary_search_IN(unsigned char *key, int key_len, IN *node);
int binary_search_PLN(unsigned char *key, int key_len, PLN *node);
LN *find_leaf(tree *t, unsigned char *key, int key_len);
int search_leaf_node(LN *node, unsigned char *key, int key_len);
void insertion_sort(struct LN_entry *base, int num);
int create_new_tree(tree *t, key_item *new_item, void *value);
int leaf_scan_divide(tree *t, LN *leaf, LN *split_node1, LN *split_node2);
int recursive_insert_IN(void *root_addr, unsigned long first_IN_id,
		         unsigned long num_IN);
int reconstruct_from_PLN(void *root_addr, unsigned long first_PLN_id,
		         unsigned long num_PLN);
int insert_node_to_PLN(PLN *new_PLNs, unsigned long parent_id, key_item *insert_key,
		         key_item *split_max_key, LN *split_node1, LN *split_node2);
int reconstruct_PLN(tree *t, unsigned long parent_id, key_item *insert_key,
		         key_item *split_max_key, LN *split_node1, LN *split_node2);
int insert_to_PLN(tree *t, unsigned long parent_id,
		         LN *split_node1, LN *split_node2);
void insert_entry_to_leaf(LN *leaf, key_item *new_item, void *value, bool flush);
int leaf_split_and_insert(tree *t, LN *leaf, key_item *new_item, void *value);
