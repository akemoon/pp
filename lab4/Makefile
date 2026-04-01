CC  = mpicc
CXX = mpicxx

CFLAGS   = -Wall -O2
CXXFLAGS = -Wall -O2 -std=c++23

all: ring line sort

ring: ./src/ring.c
	$(CC) $(CFLAGS) -o ring ./src/ring.c

line: ./src/line.c
	$(CXX) $(CXXFLAGS) -o line ./src/line.c

sort: ./src/sort.cpp
	$(CXX) $(CXXFLAGS) -o sort ./src/sort.cpp

run_ring:
	mpirun -np 5 ./ring

run_line:
	mpirun -np 5 ./line

run_sort_sfg:
	mpirun -np 5 ./sort

run_sort_dg:
	mpirun -np 25 --oversubscribe ./sort

clean:
	rm -f ring line sort
