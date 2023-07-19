// Original header only ran on __APPLE__ systems

#ifndef PTHREAD_BARRIER_H_
#define PTHREAD_BARRIER_H_

#include <pthread.h>

// Renamed from original file as it conflicted with base library
typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned int count;
    unsigned int tripCount;
} pthread_barrier_struct;

int pthread_barrier_init(pthread_barrier_struct *barrier, const pthread_barrierattr_t *attr, unsigned int count);
int pthread_barrier_destroy(pthread_barrier_struct *barrier);
int pthread_barrier_wait(pthread_barrier_struct *barrier);

#endif // PTHREAD_BARRIER_H_
