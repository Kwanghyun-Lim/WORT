BIN_DIR = ./
CFLAGS = -c

all:
	gcc $(CFLAGS) PART.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	gcc -o $(BIN_DIR)PART_synthetic_128M PART.o main.o -lrt -O3
	rm *.o
