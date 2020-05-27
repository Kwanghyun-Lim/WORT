BIN_DIR = ./
CFLAGS = -c

all:
	gcc $(CFLAGS) WOART.c -lrt -O3
	gcc $(CFLAGS) main.c -lrt -O3
	gcc -o $(BIN_DIR)WOART_synthetic_1024M WOART.o main.o -lrt -O3
	rm *.o
