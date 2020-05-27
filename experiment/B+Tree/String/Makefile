BIN_DIR=./
CFLAGS= -c 

all:
	gcc $(CFLAGS) B+Tree.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	gcc -o $(BIN_DIR)b+tree_synthetic_1024M_200 B+Tree.o main.o -lrt -O3
	rm *.o
