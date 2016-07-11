#include "familytree.h"
#include <omp.h>

void traverse_mp(tree *node) {
    if(node != NULL){
        node->IQ = compute_IQ(node->data);
        genius[node->id] = node->IQ;
        #pragma omp parallel
        #pragma omp sections nowait
        {
            #pragma omp section
            {
            traverse_mp(node->right);
            }
            
            #pragma omp section
            {
            traverse_mp(node->left);
            }
        }
    }
}

void traverse(tree *node, int numThreads) {
    omp_set_num_threads(numThreads);
    omp_set_nested(1);
    omp_set_max_active_levels(numThreads);
    #pragma omp sections nowait
    {
        #pragma omp section
        traverse_mp(node);
    }
}