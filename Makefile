#!/bin/bash/

.PHONY : clean

all : main.o

main.o : main.c
	gcc -fno-builtin -o main *.c *.h

clean :
	rm *.o main
