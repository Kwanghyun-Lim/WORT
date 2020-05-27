static art_leaf* minimum(const art_node *n) {
    // Handle base cases
    if (!n) return NULL;
    if (IS_LEAF(n)) return LEAF_RAW(n);

    int idx;
    switch (n->type) {
        case NODE4:
            return minimum(((art_node4*)n)->children[0]);
        case NODE16:
            return minimum(((art_node16*)n)->children[0]);
        case NODE48:
            idx=0;
            while (!((art_node48*)n)->keys[idx]) idx++;
            idx = ((art_node48*)n)->keys[idx] - 1;
            return minimum(((art_node48*)n)->children[idx]);
        case NODE256:
            idx=0;
            while (!((art_node256*)n)->children[idx]) idx++;
            return minimum(((art_node256*)n)->children[idx]);
        default:
            abort();
    }
}

static void recursive_iter(art_node *n, int depth) {
    // Handle base cases
    if (!n) return;
    if (IS_LEAF(n)) return;

    int idx, res;
    switch (n->type) {
        case NODE4:
            for (int i=0; i < n->num_children; i++) {
				if (n->partial_len > 0) {
					art_leaf * l1 = minimum(((art_node4*)n)->children[0]);
					art_leaf * l2 = minimum(((art_node4*)n)->children[1]);
					int longest_prefix = longest_common_prefix(l1, l2, depth);

					if (n->partial_len != longest_prefix) {
						memcpy(n->partial, l1->key, min(longest_prefix, PREFIX_MAX));
						n->partial_len = longest_prefix;
						printf("Recovory done!\n");
						exit(1);
					}
				}
                recursive_iter(((art_node4*)n)->children[i]);
            }
            break;

        case NODE16:
            for (int i=0; i < n->num_children; i++) {
                recursive_iter(((art_node16*)n)->children[i]);
            }
            break;

        case NODE48:
            for (int i=0; i < 256; i++) {
                idx = ((art_node48*)n)->keys[i];
                if (!idx) continue;
                recursive_iter(((art_node48*)n)->children[idx-1]);   //[kh] Why idx-1?
            }
            break;

        case NODE256:
            for (int i=0; i < 256; i++) {
                if (!((art_node256*)n)->children[i]) continue;
                recursive_iter(((art_node256*)n)->children[i]);
            }
            break;

        default:
            abort();
    }
    return;
}

