BIN_DIR = ./
CFLAGS = -c

all:
	gcc $(CFLAGS) art_d.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	gcc -o $(BIN_DIR)RT_synthetic_128M art_d.o main.o -lrt -O3
	rm *.o
