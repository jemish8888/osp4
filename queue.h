#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

struct qitem {
  pid_t pid;            //process waiting for the event
  struct timeval tv;    //event time
  struct timeval added; //queue insertion time
};

struct queue {
  struct qitem items[PROC_LIMIT];
  int len;
};

int rq_push(const pid_t pid);
pid_t rq_pop(void);
int bq_push(const pid_t pid, const struct timeval tv);

pid_t bq_pop(void);
const struct qitem* bq_top(void);

void queue_flush(pid_t pid);

void queues_init();

#endif
