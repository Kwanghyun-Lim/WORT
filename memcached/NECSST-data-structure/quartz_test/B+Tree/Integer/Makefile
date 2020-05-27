BIN_DIR=./
PM_DIR=/home/sekwon/study/quartz/src/lib/
SO_DIR=/home/sekwon/study/quartz/build/src/lib/
CFLAGS= -c

all:
	gcc $(CFLAGS) B+Tree.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	gcc -o $(BIN_DIR)bptree_synthetic_1024M B+Tree.o main.o $(SO_DIR)libnvmemul.so -lrt -O3
	rm *.o
