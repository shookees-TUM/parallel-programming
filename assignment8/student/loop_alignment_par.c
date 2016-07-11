#include <omp.h>

void compute(unsigned long *a, unsigned long *b, unsigned long *c, unsigned long *d, int N, int num_threads) {

	// perform loop alignment to transform this loop and parallelize it with OpenMP	

    a[1] = d[1] * b[1];
    c[0] = a[1] * d[1];
    #pragma omp parallel num_threads(num_threads)
    {
    #pragma omp for
	for (int i = 2; i < N; i++) {
		
        b[i] = 2 * c[i - 1];

		a[i] = d[i] * b[i];
		
		c[i - 1] = a[i] * d[i];
		
	}
    }
    b[N] = 2 * c[N - 1];
}
