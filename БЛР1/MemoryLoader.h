#ifndef MEMORYLOADER_H
#define MEMORYLOADER_H

#include <time.h>

clock_t memory_loader(int number, double* user_avg, double* system_avg, double* wait_avg, unsigned long* context_switches_total, unsigned long* context_switches_delta, int* parallel_processes);

#endif 