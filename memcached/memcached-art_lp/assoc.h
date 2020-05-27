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
#define CACHE_LINE_SIZE 	64

#define NODE_BITS	7
#define NUM_NODE_ENTRIES (0x1UL << NODE_BITS)

#define MAX_PREFIX_LEN 24
//unsigned long node4_count;
//unsigned long node16_count;
//unsigned long node48_count;
//unsigned long node256_count;
//unsigned long leaf_count;
//unsigned long mfence_count;
//unsigned long clflush_count;



typedef struct {
    uint32_t partial_len;
    unsigned char partial[MAX_PREFIX_LEN];
} art_node;

typedef struct {
    art_node n;
    art_node *children[NUM_NODE_ENTRIES];
} art_node16;

typedef struct {
    art_node *root;
    uint64_t size;
} art_tree;


int art_tree_init(art_tree *t);

#define init_art_tree(...) art_tree_init(__VA_ARGS__)


item* art_minimum(art_tree *t);
item* art_maximum(art_tree *t);

/* flush functions*/
void flush_buffer(void *buf, unsigned long len, bool fence);


