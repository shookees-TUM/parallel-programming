#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include "helper.h"

void reverse(char *str, int strlen)
{
	// parallelize this function and make sure to call reverse_str()
	// on each processor to reverse the substring.
	
	int np, rank;
    int i; // C99, yo
    int *chunk_sizes; // what's the size of a chunk each processor receives
    int *chunk_starts; // what's the starting point of a string (array), that the processor receives
    int chunk_pointer = 0; // sort of general pointer to where the current distributed chunk start is at.
    int remaining; // remaining character count (when it doesn't divice equally)
    char recv_str[strlen]; // string where processor will send back data


    MPI_Comm_size(MPI_COMM_WORLD, &np);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    remaining = strlen % np;
    chunk_sizes = malloc(sizeof(int) * np);
    chunk_starts = malloc(sizeof(int) * np);

    // Calculate chunk sizes and starts
    for (i = 0; i < np; i++)
    {
        chunk_sizes[i] = strlen / np;
        if (remaining > 0)
        {
            chunk_sizes[i] += 1;
            remaining -= 1;
        }
        chunk_starts[i] = chunk_pointer;
        chunk_pointer += chunk_sizes[i];
    }

    int j;
    char *my_str;
    if (0 == rank) {
        for (i = 0; i < np; i++) {
            // printf("chunk_sizes[%d] = %d\tchunk_starts[%d] = %d\n", i, chunk_sizes[i], i, chunk_starts[i]);
            my_str = malloc(sizeof(char) * chunk_sizes[i] + 1);
            for (j = 0; j < chunk_sizes[i]; j++)
            {
                my_str[j] = str[chunk_starts[i] + j];
            }
            my_str[chunk_sizes[i]] = '\0';
            // printf("%d: %s\n", i, my_str);
            free(my_str);
        }
        // divide the data among processes as described by sendcounts and displs
    }

    // int MPI_Scatterv(const void *sendbuf, const int *sendcounts, const int *displs,
    //           MPI_Datatype sendtype, void *recvbuf, int recvcount,
    //           MPI_Datatype recvtype,
    //           int root, MPI_Comm comm)
    MPI_Scatterv(str, chunk_sizes, chunk_starts, MPI_CHAR, &recv_str, strlen, MPI_CHAR, 0, MPI_COMM_WORLD);

    // Apparently a good "FEATURE" in given code is that reverse_str on size 1 returns nothing
    reverse_str(recv_str, chunk_sizes[rank]);


    // String buffer for the root process
    if (rank == 0)
    {
        chunk_pointer = chunk_sizes[0];
        // Insert my own reversed substring
        for (i = 0; i < chunk_sizes[0]; i++)
        {
            str[strlen - chunk_pointer + i] = recv_str[i];
        }
        // Receive reversed strings
        for (i = 1; i < np; i++)
        {
            MPI_Recv(&recv_str, chunk_sizes[i], MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            chunk_pointer += chunk_sizes[i];
            for (j = 0; j < chunk_sizes[i]; j++)
            {
                str[strlen - chunk_pointer + j] = recv_str[j];
            }
        }
    }
    else
    {
        // Send reversed string
        MPI_Send(&recv_str, chunk_sizes[rank], MPI_CHAR, 0, 0, MPI_COMM_WORLD);
     }

    if (rank == 0)
    {
        free(chunk_sizes);
        free(chunk_starts);
    }
}