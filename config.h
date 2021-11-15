#ifndef CONFIG_H
#define CONFIG_H

/* limit of processes in the system */
#define PROC_LIMIT 18
#define PROC_TOTAL 100

/* limit on the runtime (in seconds) */
#define TIME_LIMIT 30

#define LOGNAME "oss.log"

/* Percentage a process can terminate */
#define CHANCE_TO_TERMINATE 15

#define CPUBOUND_CHANCE 30

/* 500 ms time slice per burst */
#define SLICE_NS 500000

/* we have 2 ready queues - high and low */
#define RQ_COUNT 2

#define MAX_LINES 10000

/* interrupt probability */
static const unsigned int interrupt_prob[2] = {15, 60};

#define maxTimeBetweenNewProcsSecs 2
#define maxTimeBetweenNewProcsNS   10000

#endif
