OBJECTS = threadpool.o
CFLAGS  = -std=c11 -ggdb3 -Wall -Wpedantic -O3
LDLIBS  =
CC=gcc

all: http-test mult-test

http-test: $(OBJECTS)

mult-test: $(OBJECTS)
