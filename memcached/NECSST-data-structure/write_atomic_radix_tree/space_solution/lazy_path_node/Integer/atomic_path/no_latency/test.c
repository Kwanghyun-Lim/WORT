#include <stdio.h>
#include <stdint.h>

#define MAX_PREFIX_LEN 7
typedef struct {
	unsigned char partial_len;
	unsigned char partial[MAX_PREFIX_LEN];
} path_comp;

/**
 * This struct is included as part
 * of all the various node sizes
 */
typedef struct {
    uint8_t type;
    uint8_t num_children;
	path_comp path;
} art_node;

/**
 * Small node with only 4 children
 */
typedef struct {
    art_node n;
    unsigned char keys[4];
	unsigned char dummy[50];
//    art_node *children[4];
} art_node4;

/**
 * Node with 16 children
 */
typedef struct {
    art_node n;
    unsigned char keys[16];
	unsigned char dummy[38];
//    art_node *children[16];
} art_node16;

/**
 * Node with 48 children, but
 * a full 256 byte field.
 */
typedef struct {
    art_node n;
    unsigned char keys[256];
	unsigned char dummy[54];
//    art_node *children[48];
} art_node48;

/**
 * Full node with 256 children
 */
typedef struct {
    art_node n;
	unsigned char dummy[54];
//    art_node *children[256];
} art_node256;

int main(void)
{
	printf("sizeof(art_node) = %lu\n", sizeof(art_node));
	printf("sizeof(art_node4) = %lu\n", sizeof(art_node4));
	printf("sizeof(art_node16) = %lu\n", sizeof(art_node16));
	printf("sizeof(art_node48) = %lu\n", sizeof(art_node48));
	printf("sizeof(art_node256) = %lu\n", sizeof(art_node256));
}
