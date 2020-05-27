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

#define NODE4   1
#define NODE16  2
#define NODE48  3
#define NODE256 4

#define BITS_PER_LONG  64
#define BITOP_WORD(nr) ((nr) / BITS_PER_LONG)
#define MAX_PREFIX_LEN 10
//unsigned long node4_count;
//unsigned long node16_count;
//unsigned long node48_count;
//unsigned long node256_count;
//unsigned long leaf_count;
//unsigned long mfence_count;
//unsigned long clflush_count;
//unsigned long path_comp_count;

typedef struct {
    uint8_t type;
    uint8_t num_children;
	uint32_t partial_len;
	unsigned char partial[MAX_PREFIX_LEN];
} art_node;

typedef struct {
    art_node n;
    unsigned char keys[4];
    art_node *children[4];
} art_node4;

typedef struct {
    art_node n;
    unsigned char keys[16];
    art_node *children[16];
} art_node16;


typedef struct {
    art_node n;
    unsigned char keys[256];
    art_node *children[48];
} art_node48;


typedef struct {
    art_node n;
    art_node *children[256];
} art_node256;


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

static inline unsigned long ffz(unsigned long word)
{
	asm("rep; bsf %1,%0"
		: "=r" (word)
		: "r" (~word));
	return word;
}
