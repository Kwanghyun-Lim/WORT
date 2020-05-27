/* associative array */
void assoc_init(const int hashpower_init);
item *assoc_find(const char *key, const size_t nkey);
int assoc_insert(item *item);
void assoc_delete(const char *key, const size_t nkey);

extern unsigned int hashpower;
extern unsigned int item_lock_hashpower;


/* W_RADIX_TREE_H */
#include <stdbool.h>
#include <stdint.h>

#define NODE4	1
#define NODE16	2
//#define NODE32	3
#define NODE128	4


#define MAX_PREFIX_LEN 24
#define BITS_PER_LONG		64
#define CACHE_LINE_SIZE 	64


#if defined(__GNUC__) && !defined(__clang__)
# if __STDC_VERSION__ >= 199901L && 402 == (__GNUC__ * 100 + __GNUC_MINOR__)
/*
 * GCC 4.2.2's C99 inline keyword support is pretty broken; avoid. Introduced in
 * GCC 4.2.something, fixed in 4.3.0. So checking for specific major.minor of
 * 4.2 is fine.
 */
#  define BROKEN_GCC_C99_INLINE
# endif
#endif


static inline unsigned long __ffs(unsigned long word)
{
	asm("rep; bsf %1,%0"
		: "=r" (word)
		: "rm" (word));
	return word;
}

static inline unsigned long ffz(unsigned long word)
{
	asm("rep; bsf %1,%0"
		: "=r" (word)
		: "r" (~word));
	return word;
}



typedef struct {
    uint8_t type;
    uint32_t partial_len;
    unsigned char partial[MAX_PREFIX_LEN];
} art_node;

typedef struct {
	unsigned char key;
	char i_ptr;
} slot_array;

/**
 * Small node with only 4 children, but
 * 8byte slot array field.
 */
typedef struct {
    art_node n;
	slot_array slot[4];
    art_node *children[4];
} art_node4;

/**
 * Node with 16 children, but
 * a full 128 byte field.
 */
typedef struct {
    art_node n;
	unsigned int bitmap[4][2];
    unsigned char keys[128];
    art_node *children[16];
} art_node16;

/**
 * Node with 32 children, but
 * a full 128 byte field.
typedef struct {
    art_node n;
	unsigned int bitmap[4][2];
    unsigned char keys[128];
    art_node *children[32];
} art_node32;
*/

/**
 * Full node with 128 children
 */
typedef struct {
    art_node n;
    art_node *children[128];
} art_node128;


/*
typedef struct {
    void *value;
    uint32_t key_len;	
	unsigned char key[];
} art_leaf;
*/

/**
 * Main struct, points to root.
 */
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
void flush_buffer_nocount(void *buf, unsigned long len, bool fence);
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset);
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
		unsigned long offset);



