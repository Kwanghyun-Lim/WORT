BIN_DIR = ./
CFLAGS = -c

all:
	gcc $(CFLAGS) art.c -lrt
	gcc $(CFLAGS) main.c -lrt
	gcc -o $(BIN_DIR)ART_synthetic_128M art.o main.o -lrt
	rm *.o
