CC=clang
CFLAGS=-g -Wall $(shell pkg-config --cflags fuse)
LDFLAGS=$(shell pkg-config --libs fuse) -lgdbm

.PHONY: all clean

all: dedupefs

clean:
	-rm *.o
