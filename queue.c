#include <stdio.h>
#include <strings.h>
#include "common.h"
#include "queue.h"

/* read queues - high and low */
static struct queue RQ[RQ_COUNT];
/* blocked queue */
static struct queue BQ;

void queues_init(){
  bzero(RQ, sizeof(RQ));
  bzero(&BQ, sizeof(BQ));
}

/* Add a process PID to ready queue */
int rq_push(const pid_t pid){

  struct proc * proc = find_proc(pid);

  /* Use type of process (CPU/IO bound) to determine which queue to use */
  struct queue * q = &RQ[proc->bound];

  if(q->len < PROC_LIMIT){
    printf("OSS: Process %d queued into RQ %d\n", proc->pid, proc->bound);

    q->items[q->len].pid   = proc->pid;
    /* here age and insert time are the same */
    q->items[q->len].tv    =
    q->items[q->len].added = simulator_obj->clock;

    q->len++;

    return 0;
  }else{
    fprintf(stderr, "ERROR: Ready queue is full\n");
    return -1;
  }
}

/* Find first queue with items */
static struct queue * next_rq(){
  int i;
  for(i=0; i < RQ_COUNT; i++){
    if(RQ[i].len != 0){
      return &RQ[i];
    }
  }
  return NULL;
}

/* Find process that waited most */
static int rq_waited_most(struct queue * q){
  int i, max = 0;
  for(i=1; i < q->len; i++){
    if(timercmp(&q->items[max].tv, &q->items[i].tv, >)){
      max = i;
    }
  }
  return max;
}

static void rq_shift(struct queue * q, const int pos){
  int i;
  for(i=pos + 1; i < q->len; i++){
    q->items[i-1] = q->items[i];
  }
  q->len--;
}

/* Remove a process from ready queue */
pid_t rq_pop(void){

  struct timeval wt;
  struct queue * q = next_rq();
  struct proc * proc = NULL;
  int waited_most = 0;
  pid_t pid = 0;

  if(q == NULL){
    /* if all queues are empty */
    return 0;
  }

  /* Find process that waited most in this queue */
  while((proc == NULL) && (q->len > 0)){
    waited_most = rq_waited_most(q);
    if(waited_most == -1){
      return 0;
    }
    pid = q->items[waited_most].pid;

    proc = find_proc(pid);
    if(proc == NULL){ /* if not found, proc terminated*/
      rq_shift(q, waited_most);
    }
  }

  if(proc == NULL){
    return 0;
  }

  /* update its wait time */
  timersub(&simulator_obj->clock, &q->items[waited_most].added, &wt);
  tincrement(&proc->timer[T_WAIT], &wt);

  /* shift queue left */
  rq_shift(q, waited_most);

  printf("OSS: Pop PID %d from ready queue %i at time %li:%li,\n",
    pid, waited_most, simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);

  return pid;
}

/* Add process to blocked queue, until time tv*/
int bq_push(const pid_t pid, const struct timeval until){
  if(BQ.len >= PROC_LIMIT){
    printf("OSS: Blocked queue full!\n");
    return -1;
  }

  struct qitem * item = &BQ.items[BQ.len];
  item->pid = pid;
  item->tv = until;
  item->added = simulator_obj->clock;  //save insertion time
  BQ.len++;
  return 0;
}

//find a process, which event has come
static int bq_next(){
  int i;
  for(i=0; i < BQ.len; i++){
    if(timercmp(&simulator_obj->clock, &BQ.items[i].tv, >=)){
      printf("OSS: Process with PID %d event(%li:%li) ready at time %li:%li\n", BQ.items[i].pid,
        BQ.items[i].tv.tv_sec, BQ.items[i].tv.tv_usec,
        simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
      return i;
    }
  }
  return -1;
}

pid_t bq_pop(void){
  int i;
  struct timeval wt;
  pid_t pid;
  struct proc * proc = NULL;

  while((proc == NULL) && (BQ.len > 0)){
    i = bq_next();
    if(i == -1){
      /* nobody can be unblocked now */
      return 0;
    }
    pid = BQ.items[i].pid;

    proc = find_proc(pid);
    if(proc == NULL){ /* if not found, proc terminated*/
      rq_shift(&BQ, i);
    }
  }

  if(proc == NULL){
    return 0;
  }

  /* update its wait time */
  timersub(&simulator_obj->clock, &BQ.items[i].added, &wt);
  tincrement(&proc->timer[T_BLOCKED], &wt);

  /* shift queue len */
  rq_shift(&BQ, i);

  return pid;
}

/* Return process at top of blocked queue */
const struct qitem* bq_top(void){
  if(BQ.len == 0){
    return NULL;
  }
  return &BQ.items[0];
}
