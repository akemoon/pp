#include <mpi.h>
#include <string.h>
#include <stdio.h>

int main (int argc, char **argv)
{
    char message [20];
    int myrank;
    MPI_Status status;
    MPI_Init (&argc, &argv);
    MPI_Comm_rank (MPI_COMM_WORLD, &myrank);
    if (myrank==0)
    {
        strncpy(message,"Hello, there!\0",15);
        MPI_Send (message, strlen(message)+1, MPI_CHAR, 1,99,
        MPI_COMM_WORLD);
    }
    else
    {
        MPI_Recv (message, 20, MPI_CHAR, 0,99,
        MPI_COMM_WORLD, &status);
        printf ("received: %s \n", message);
    }
    MPI_Finalize ();
}
