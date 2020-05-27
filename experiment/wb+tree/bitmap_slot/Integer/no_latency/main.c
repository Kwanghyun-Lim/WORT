#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include "wbtree.h"

#define INPUT_NUM	128000000

int main(void)
{
	struct timespec t1, t2;
	int i, perf;
	char *dummy;
	unsigned long *keys, *new_value;
	unsigned long elapsed_time;
	void *ret;
	FILE *fp;
	unsigned long *buf;
	unsigned long max;
	unsigned long min;

	if ((fp = fopen("/home/sekwon/Public/input_file/input_random_synthetic_128M.txt","r")) == NULL)
	{
		puts("error");
		exit(0);
	}

	keys = malloc(sizeof(unsigned long) * INPUT_NUM);
	buf = malloc(sizeof(unsigned long) * INPUT_NUM);
	memset(buf, 0, sizeof(unsigned long) * INPUT_NUM);
	for(i = 0; i < INPUT_NUM; i++) {
//		keys[i] = i;
		fscanf(fp, "%lu", &keys[i]);
	}
	fclose(fp);

	max = keys[0];
	min = keys[0];
	for (i = 1; i < INPUT_NUM; i++) {
		if (keys[i] > max)
			max = keys[i];
		if (keys[i] < min)
			min = keys[i];
	}

	tree *t = initTree();

	/* Insertion */
	dummy = (char *)malloc(15*1024*1024);
	memset(dummy, 0, 15*1024*1024);
	flush_buffer_nocount((void *)dummy, 15*1024*1024, true);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	for(i = 0; i < INPUT_NUM; i++)
		Insert(t, keys[i], &keys[i]);
	clock_gettime(CLOCK_MONOTONIC, &t2);
	elapsed_time = (t2.tv_sec - t1.tv_sec) * 1000000000;
	elapsed_time += (t2.tv_nsec - t1.tv_nsec);
	printf("Insertion Time = %lu ns\n",elapsed_time);
//#ifdef sekwon
	/* Check space overhead */
	printf("sizeof(node) = %d\n", sizeof(node));
	printf("Total space = %lu byte\n", node_count * sizeof(node));
	printf("Space efficiency = %lu\n", (node_count * sizeof(node)) / INPUT_NUM);
	printf("node count = %lu\n", node_count);
	printf("clflush count = %lu\n", clflush_count);
	printf("mfence count = %lu\n", mfence_count);
	printf("split count = %lu\n", split_count);

/*
	// Lookup
	memset(dummy, 0, 15*1024*1024);
	flush_buffer_nocount((void *)dummy, 15*1024*1024, true);
//	printf("Search start: ");
//	scanf("%lu", &sum_depth);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	for(i = 0; i < INPUT_NUM; i++) {
		ret = (void *)Lookup(t, keys[i]);		
		if (ret == NULL) {
			printf("There is no key[%d] = %lu\n", i, keys[i]);
			exit(1);
		}
//		else {
//			printf("Search value = %lu\n", *(unsigned long*)ret);
//		}
	}
	clock_gettime(CLOCK_MONOTONIC, &t2);
	elapsed_time = (t2.tv_sec - t1.tv_sec) * 1000000000;
	elapsed_time += (t2.tv_nsec - t1.tv_nsec);
	printf("Search Time = %lu ns\n", elapsed_time);
	printf("Avg. depth = %lu\n", sum_depth / INPUT_NUM);
	printf("Total sum of depth = %lu\n", sum_depth);
*/

//#ifdef sekwon
	// Range scan 0.001%
	memset(dummy, 0, 15*1024*1024);
	flush_buffer_nocount((void *)dummy, 15*1024*1024, true);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	Range_Lookup(t, min, INPUT_NUM / 100000, buf);
	clock_gettime(CLOCK_MONOTONIC, &t2);
	elapsed_time = (t2.tv_sec - t1.tv_sec) * 1000000000;
	elapsed_time += (t2.tv_nsec - t1.tv_nsec);
	printf("Range scan 0.001% = %lu ns\n", elapsed_time);

	// Range scan 0.01%
	memset(dummy, 0, 15*1024*1024);
	flush_buffer_nocount((void *)dummy, 15*1024*1024, true);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	Range_Lookup(t, min, INPUT_NUM / 10000, buf);
	clock_gettime(CLOCK_MONOTONIC, &t2);
	elapsed_time = (t2.tv_sec - t1.tv_sec) * 1000000000;
	elapsed_time += (t2.tv_nsec - t1.tv_nsec);
	printf("Range scan 0.01% = %lu ns\n", elapsed_time);
#ifdef sekwon
	/* Range scan 10% */
	memset(dummy, 0, 15*1024*1024);
	flush_buffer_nocount((void *)dummy, 15*1024*1024, true);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	Range_Lookup(t, min, INPUT_NUM / 10, buf);
	clock_gettime(CLOCK_MONOTONIC, &t2);
	elapsed_time = (t2.tv_sec - t1.tv_sec) * 1000000000;
	elapsed_time += (t2.tv_nsec - t1.tv_nsec);
	printf("Range scan 10% = %lu ns\n", elapsed_time);

	/* Update */
	new_value = malloc(sizeof(unsigned long) * INPUT_NUM);
	for (i = 0; i < INPUT_NUM; i++)
		new_value[i] = i;
	memset(dummy, 0, 15*1024*1024);
	flush_buffer_nocount((void *)dummy, 15*1024*1024, true);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	for (i = 0; i < INPUT_NUM; i++)
		Update(t, keys[i], &new_value[i]);
	clock_gettime(CLOCK_MONOTONIC, &t2);
	elapsed_time = (t2.tv_sec - t1.tv_sec) * 1000000000;
	elapsed_time += (t2.tv_nsec - t1.tv_nsec);
	printf("Update time = %lu ns\n", elapsed_time);

	/* Delete */
	memset(dummy, 0, 15*1024*1024);
	flush_buffer((void *)dummy, 15*1024*1024, true);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	for (i = 0; i < 100; i++)
		Delete(t, keys[i]);
	clock_gettime(CLOCK_MONOTONIC, &t2);
	elapsed_time = (t2.tv_sec - t1.tv_sec) * 1000000000;
	elapsed_time += (t2.tv_nsec - t1.tv_nsec);
	printf("Delete time = %lu ns\n", elapsed_time);
	for (i = 0; i < 100000100; i++)
		printf("buf[%d] = %lu\n", i, buf[i]);
#endif
	return 0;
}
