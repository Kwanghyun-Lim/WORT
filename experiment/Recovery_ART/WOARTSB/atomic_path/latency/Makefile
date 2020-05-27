BIN_DIR=./
SO_DIR=/home/sekwon/study/quartz/build/src/lib/
CFLAGS= -c

all:
	gcc $(CFLAGS) PART.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	gcc -o $(BIN_DIR)WOARTSB_synthetic_128M PART.o main.o $(SO_DIR)libnvmemul.so -lrt -O3
	rm *.o
