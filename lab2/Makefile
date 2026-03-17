CC = gcc
CFLAGS = -Wall -Wextra

CPPC = g++
CPPFLAGS = -std=c++23

all: race

racesem: race.c
	$(CC) $(CFLAGS) -o racesem race.c

racesig: race.cpp
	$(CPPC) $(CPPFLAGS) -o racesig race.cpp

clean:
	rm -f race*