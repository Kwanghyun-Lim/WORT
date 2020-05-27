BIN_DIR=./
SO_DIR=/home/sekwon/study/quartz/build/src/lib/
CFLAGS= -c

all:
	gcc $(CFLAGS) art_lpn.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	gcc -o $(BIN_DIR)WOARTC_synthetic_128M art_lpn.o main.o $(SO_DIR)libnvmemul.so -lrt -O3
	rm *.o
