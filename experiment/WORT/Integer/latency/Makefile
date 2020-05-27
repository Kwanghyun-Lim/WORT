BIN_DIR=./
SO_DIR=/home/sekwon/study/quartz/build/src/lib/
CFLAGS= -c

all:
	gcc $(CFLAGS) art_lp.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	#gcc -o $(BIN_DIR)WORT_synthetic_128M art_lp.o main.o $(SO_DIR)libnvmemul.so -lrt -O3
	gcc -o $(BIN_DIR)WORT_synthetic_128M art_lp.o main.o -lrt -O3
	rm *.o
