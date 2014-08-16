CFLAGS=-g -Wall

all: play

play: main.o
	$(CC) -g -o $@ main.o -framework CoreAudio

clean:
	rm -rf main.o play
