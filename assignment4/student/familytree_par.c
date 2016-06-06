#include "familytree.h"
#include <omp.h>

void traverse_mp(tree *node) {
    if(node != NULL){
        //printf("Traversing %s\n", node->name);
        node->IQ = compute_IQ(node->data);
        genius[node->id] = node->IQ;
        #pragma omp task shared(node)
        traverse_mp(node->right);

        #pragma omp task shared(node) 
        traverse_mp(node->left);
    }
    #pragma omp taskwait
}

void traverse(tree *node, int numThreads) {
    omp_set_num_threads(numThreads);
    #pragma omp parallel shared(node)
    {
        #pragma omp single nowait
        traverse_mp(node);
    }
}