#ifndef CPULOADER_H
#define CPULOADER_H

#include <time.h>

char* create_text();
clock_t cpu_loader(const char* text, double* user_avg, double* system_avg, double* wait_avg);

#endif