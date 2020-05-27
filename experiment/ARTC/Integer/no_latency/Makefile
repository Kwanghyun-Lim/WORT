BIN_DIR = ./
CFLAGS = -c

all:
	gcc $(CFLAGS) ARTC.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	gcc -o $(BIN_DIR)ARTC_synthetic_128M ARTC.o main.o -lrt -O3
	rm *.o
