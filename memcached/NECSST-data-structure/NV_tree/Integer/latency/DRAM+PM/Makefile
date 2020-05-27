BIN_DIR=./
SO_DIR=/home/sekwon/study/quartz/build/src/lib/
CFLAGS= -c

all:
	gcc $(CFLAGS) NV-tree.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	gcc -o $(BIN_DIR)NV_synthetic_128M_DP NV-tree.o main.o $(SO_DIR)libnvmemul.so -lrt -O3
	rm *.o
