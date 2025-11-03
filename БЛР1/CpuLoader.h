#ifndef CPULOADER_H
#define CPULOADER_H

#include <time.h>
typedef struct {
    double user;
    double system;
    double wait;
    unsigned long context_switches;
} cpu_stats_t;
char* create_text();
void* cpu_monitoring(void* arg);
clock_t cpu_loader(const char* text, double* user_avg, double* system_avg, double* wait_avg, 
                   unsigned long* context_switches_total, unsigned long* context_switches_delta,
                   int* max_parallel_processes); 


void* process_monitor_thread(void* arg);
cpu_stats_t get_cpu_stats_from_proc(void);

#endif