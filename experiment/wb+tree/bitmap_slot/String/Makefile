BIN_DIR=./
CFLAGS= -c 

all:
	gcc $(CFLAGS) wbtree.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	gcc -o $(BIN_DIR)wbtree_synthetic_1024M_4 wbtree.o main.o -lrt -O3
	rm *.o
