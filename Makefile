CFLAGS=-g -Wall

all: play

play: gdw.o
	$(CC) -g -o $@ gdw.o -framework CoreAudio

clean:
	rm -rf gdw.o play
