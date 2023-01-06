
CROSS_COMPILE ?= mips-v720s229-linux-gnu-

CC = $(CROSS_COMPILE)gcc
CPLUSPLUS = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld
AR = $(CROSS_COMPILE)ar cr
STRIP = $(CROSS_COMPILE)strip

#CFLAGS = $(INCLUDE) -O2 -Wall -march=mips32r2

#$(INCLUDE) = -I ./

all : main.o
	gcc -fno-builtin -o main *.c *.h
