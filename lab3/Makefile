CC = mpicc
CXX = mpicxx
CFLAGS = -Wall -g
CXXFLAGS = -Wall -g -std=c++23

all: example race matrix_mult_v1 matrix_mult_v2

example: ./src/example.c
	$(CC) $(CFLAGS) -o example ./src/example.c

race: ./src/race.cpp
	$(CXX) $(CXXFLAGS) -o race ./src/race.cpp

matrix_mult_v1: ./src/matrix_mult_v1.c
	$(CC) $(CFLAGS) -o matrix_mult_v1 ./src/matrix_mult_v1.c

matrix_mult_v2: ./src/matrix_mult_v2.c
	$(CC) $(CFLAGS) -o matrix_mult_v2 ./src/matrix_mult_v2.c

clean:
	rm -f example race matrix_mult_v1 matrix_mult_v2
