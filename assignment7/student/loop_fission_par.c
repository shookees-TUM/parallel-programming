#include <omp.h>

/* Dependence graph
 * S1 <- S2: antidependence, 1
 * S1 <- S4: antidependence, 1
 * S2 <- S1: true dependence, all (inf)
 * S3 <- S2: antidependence, 2
 * S3 <- S4: antidependence, all (inf)
 * S4 <- S3: antidependence, 2
 */

void compute(unsigned long **a, unsigned long **b, unsigned long **c, unsigned long **d, int N, int num_threads) {

	for (int j = 1; j < N; j++) {
		for (int i = 1; i < N; i++) {
			c[i][j] = 3 * d[i][j];                       // S3
			d[i][j]   = 2 * c[i-1][j];                   // S4
		}
	}
    for (int j = 1; j < N; j++) {
        for (int i = 1; i < N; i++) {
            a[i][j]   = a[i][j] * b[i][j+1]+d[i][j-1];   // S1
            b[i][j] = 2 * a[i][j] * c[i - 1][j];         // S2
        }
    }
}
