CFLAGS=-c -Wall -O2
LIBS = -lm -lSDL2 -lpigpio -lpthread

all: pi_car

pi_car: main.o
	$(CC) $(LIBS) main.o -o pi_car

main.o: main.c
	$(CC) $(CFLAGS) main.c

clean:
	rm -rf *o pi_car
