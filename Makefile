CC=gcc
CFLAGS=-Wall -ggdb
OBJECTS=common.o

default: oss user

queue.o: queue.c queue.h
	$(CC) $(CFLAGS) -c queue.c

bv.o: bv.c bv.h
	$(CC) $(CFLAGS) -c bv.c

oss: $(OBJECTS) oss.c bv.o queue.o
	$(CC) $(CFLAGS) -o oss oss.c bv.o queue.o $(OBJECTS)

user: user.c common.o
	$(CC) $(CFLAGS) -o user user.c $(OBJECTS)

common.o: common.c common.h config.h
	$(CC) $(CFLAGS) -c common.c

clean:
	rm *.o oss user
