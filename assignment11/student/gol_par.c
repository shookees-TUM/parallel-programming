#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <mpi.h>

#include "helper.h"

extern int np, rank;

unsigned int gol(unsigned char *grid, unsigned int dim_x, unsigned int dim_y, unsigned int time_steps)
{    

    unsigned int quotient, remainder, y_start, y_stop;

    quotient = dim_y / np;
    remainder = dim_y % np;

    y_start = (rank + 0) * quotient + MIN(rank + 0, remainder);
    y_stop  = (rank + 1) * quotient + MIN(rank + 1, remainder);

    unsigned int rows = y_stop - y_start;
    unsigned int all_rows = y_stop - y_start + 2;

    unsigned char (*grid_in)[dim_x]  = malloc(sizeof(unsigned char) * all_rows * dim_x);
    unsigned char (*grid_out)[dim_x] = malloc(sizeof(unsigned char) * all_rows * dim_x);

    int *counts = malloc(np * sizeof(*counts));
    int *displs = malloc(np * sizeof(*displs));

    for (int i = 0; i < np; ++i)
    {
        int y_st  = (i + 0) * quotient + MIN(i + 0, remainder);
        int y_sp  = (i + 1) * quotient + MIN(i + 1, remainder);

        counts[i] = (y_sp - y_st) * dim_x;
        displs[i] = y_st * dim_x;
    }

    MPI_Scatterv(grid, counts, displs, MPI_CHAR, grid_in + 1, rows * dim_x, MPI_CHAR, 0, MPI_COMM_WORLD);

    int rank_top = (rank - 1 + np) % np;
    int rank_bot = (rank + 1 + np) % np;

    for (int t = 0; t < time_steps; ++t)
    {
        MPI_Sendrecv(grid_in + 1, dim_x, MPI_CHAR, rank_top, 0, grid_in + all_rows - 1, dim_x, MPI_CHAR, rank_bot, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Sendrecv(grid_in + all_rows - 2, dim_x, MPI_CHAR, rank_bot, 0, grid_in, dim_x, MPI_CHAR, rank_top, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        for (int y = 1; y <= rows; ++y)
        {
            for (int x = 0; x < dim_x; ++x)
            {
                evolve((unsigned char*)grid_in, (unsigned char*)grid_out, dim_x, all_rows, x, y);
            }
        }
        swap((void**)&grid_in, (void**)&grid_out);
    }

    MPI_Gatherv(grid_in + 1, rows * dim_x, MPI_CHAR, grid, counts, displs, MPI_CHAR, 0, MPI_COMM_WORLD);

    free(grid_in);
    free(grid_out);

    if (rank == 0)
    {
        return cells_alive(grid, dim_x, dim_y);
    }
    
    return 0; 
}