#include <omp.h>

void compute(unsigned long **a, unsigned long **b, unsigned long **c, unsigned long **d, int N, int num_threads) {

	for (int i = 1; i < N; i++) {
		//do not shift, just precalculate a[i][1]
		a[i][1] = 2 * b[i][1] + d[i][1];
		for (int j = 2; j < N; j++) {
			a[i][j] = 2 * b[i][j] +d[i][j];
			d[i][j    ] = a[i][j    ] * c[i][j - 1];
			c[i][j - 2] = a[i][j - 2] - a[i][j    ];
		}
		// add the missing part of the loop
		d[i][N] = a[i][N] * c[i][N - 1];
		c[i][N - 2] = a[i][N - 2] - a[i][N];
	}
}
