#ifndef CPULOADER_H
#define CPULOADER_H

#include <time.h>

char* create_text();
void* cpu_monitoring(void* arg);
clock_t cpu_loader(const char* text, double* user_avg, double* system_avg, double* wait_avg, 
                   unsigned long* context_switches_total, unsigned long* context_switches_delta,
                   int* max_parallel_processes); 

int count_simultaneous_processes(void);
void* process_monitor_thread(void* arg);

#endif