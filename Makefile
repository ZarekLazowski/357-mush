FLAGS = -Wall -pedantic -g

all: mush

mush: mush.o parseline.o
	gcc $(FLAGS) -o mush mush.o parseline.o

mush.o: mush.c
	gcc $(FLAGS) -c mush.c

parseline.o: parseline.c parseline.h
	gcc $(FLAGS) -c parseline.c parseline.h
