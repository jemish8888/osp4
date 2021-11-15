#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <time.h>

#include "config.h"
#include "common.h"
#include "queue.h"
#include "bv.h"

/* Timers for the statistics */
enum stat_timer {ST_WAIT, ST_EXEC, ST_CPU, ST_IDLE, ST_BLOCK0, ST_BLOCK1, ST_COUNT};
static struct timeval stat_time[ST_COUNT];

/* processes counters for started and exited */
static int proc_started = 0, proc_exited[RQ_COUNT] = {0,0};
  const char * opt_log = LOGNAME;

static sigset_t blockmask, oldmask;
static struct timeval forktime; /* next forktime */
static unsigned int num_lines = 0;  /* how many lines in log */

/* block child termination signals */
static void block_signals(){
  sigemptyset(&blockmask);
  sigaddset(&blockmask, SIGCHLD);
  sigprocmask(SIG_SETMASK, &blockmask, &oldmask);
}

/* unblock child termination signals */
static void unblock_signals(){
  sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

static void ln_check(){
  if(++num_lines > MAX_LINES){
    num_lines = 0;
    /* redirect rest of output to null */
    stdout = freopen("/dev/null", "w", stdout);
  }
}

static int docommand(){

  char buf[10];

  //get child id (process table index)
  const int pindex = bv_index();
  if(pindex == -1){ //if not control blocks are free

    ln_check(); printf("OSS: No free control blocks at time %li:%li\n", simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
    return 0; //no process started
  }
  struct proc * proc = &simulator_obj->procs[pindex];

  bzero(proc, sizeof(struct proc));
  /* parent fills the user details */
  proc->id  = pindex;
  proc->timer[T_START] = simulator_obj->clock;
  /* randomly select bound of process */
  proc->bound = ((rand() % 100) < CPUBOUND_CHANCE) ? B_CPU : B_IO;

  const pid_t pid = fork();
  switch(pid){
    case -1:
      perror(perror_buf);
      return -1;

    case 0: /* do child runs the process */
      /* create the argument for process */
      snprintf(buf, sizeof(buf), "%d", pindex);

      unblock_signals();

      execl("user", "user", buf, NULL);
      perror(perror_buf);
      exit(0);

    default:
      proc->pid = pid;
      ln_check(); printf("OSS: Generating process with PID %d at time %li:%li\n", pid, simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
      /* push at end of ready queue */
      rq_push(pid);
      return 1;
  }

  return 0;
}

static void stat_onexit(struct proc * proc){
  /* update wait time */
  tincrement(&stat_time[ST_WAIT], &proc->timer[T_WAIT]);
  /* update blocked time */
  tincrement(&stat_time[ST_BLOCK0 + proc->bound], &proc->timer[T_BLOCKED]);
  /* update execution time */
  tincrement(&stat_time[ST_EXEC], &proc->timer[T_EXEC]);

  ln_check(); printf("OSS: Process %d exited with times : waiting=%li:%li, blocked=%li:%li, exec=%li:%li\n",
    proc->pid, proc->timer[T_WAIT].tv_sec,  proc->timer[T_WAIT].tv_usec,
               proc->timer[T_BURST].tv_sec, proc->timer[T_BURST].tv_usec,
               proc->timer[T_EXEC].tv_sec,  proc->timer[T_EXEC].tv_usec);
}

static void do_wait(const int flags){
  pid_t pid;
  int status;
  while((pid = waitpid(-1, &status, flags)) > 0){

    const int pindex = find_id(pid);

    if(pindex >= 0){ /* if process is found */
      struct proc * proc = &simulator_obj->procs[pindex];

      /* update simulation statistics with process times */
      stat_onexit(proc);

      proc_exited[proc->bound]++;

      /* mark the process as unused in bitvector */
      bv_off(pindex);

    }else{
      ln_check(); printf("OSS: PID=%d not found in procs[]\n", pid);
    }
  }
}

static void on_interrupt(const int sig){

  block_signals();


  if(sig == SIGINT){
    /* raise the interrupt flag */
    is_signalled = 1;
    fprintf(stderr, "P%d: Ctrl-C received\n", getpid());
  }else if(sig == SIGALRM){
    /* send termination signal to all child processes */
    kill(0, SIGTERM);
    is_signalled = 1;

  }else if(sig == SIGTERM){
    /* raise the interrupt flag */
    is_signalled = 1;
    fprintf(stderr, "P%d: SIGTERM received\n", getpid());
  }else if(sig == SIGCHLD){
    do_wait(WNOHANG);
  }

  unblock_signals();
}

/* Exchange messages with the scheduled user */
static int scheduler_msg(const pid_t pid){
  struct msgbuf buf;
  struct timeval tv;

  struct proc * proc = find_proc(pid);

  /* make a message with timeslice */
  bzero(&buf, sizeof(buf));
  buf.slice.tv_usec = SLICE_NS;

  /* give a slice to user */
  buf.mtype = pid;
  if(msg_send(&buf) == -1){
    perror(perror_buf);
    return -1;
  }

  /* now wait for the reply from user */
  buf.mtype = TYPE_BURSTED;
  if(msg_recv(&buf) == -1){
    perror(perror_buf);
    return -1;
  }

  tincrement(&proc->timer[T_EXEC], &proc->timer[T_BURST]);  //increment execution time with process burst

  switch(proc->action){

    case ACT_EXEC:
      ln_check(); printf("OSS: Receiving that process with PID %d ran for %li nanoseconds\n", pid, proc->timer[T_BURST].tv_usec);
      if(proc->timer[T_BURST].tv_usec != SLICE_NS){
        ln_check(); printf("OSS: not using its entire time quantum\n");
      }

      tincrement(&simulator_obj->clock, &proc->timer[T_BURST]);

      rq_push(pid);

    case ACT_TERM:
      ln_check(); printf("OSS: Receiving that process with PID %d terminated after running for %li nanoseconds\n", pid, proc->timer[T_BURST].tv_usec);
      tincrement(&simulator_obj->clock, &proc->timer[T_BURST]);  //increment clock with process burst
      break;

    case ACT_INT:
      /* calculate the IO end time */
      timeradd(&simulator_obj->clock, &proc->timer[T_IOEND], &tv);

      /* advance clock with burst time */
      ln_check(); printf("OSS: Advance with burst of %li:%li at time %li:%li\n",
        proc->timer[T_BURST].tv_sec, proc->timer[T_BURST].tv_usec,
        simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);

      tincrement(&simulator_obj->clock, &proc->timer[T_BURST]);

      ln_check(); printf("OSS: Putting process with PID %d into blocked queue until %li:%li\n", pid, tv.tv_sec, tv.tv_usec);

      /* put process at blocked queue */
      bq_push(pid, tv);
      break;
  }

  return 0;
}

/* Wake a process from ready and blocked queues */
static int scheduler_wakeup(){
  struct timeval t1, t2, t3;

  int nq = 0;

  gettimeofday(&t1, NULL);

  /* First try to pop from ready queue */
  pid_t pid = rq_pop();
  if(pid > 0){
    nq = scheduler_msg(pid);

    gettimeofday(&t2, NULL);
    timersub(&t2, &t1, &t3);

    /* update time with dispatch duration */
    tincrement(&simulator_obj->clock, &t3);
    ln_check(); printf("OSS: Dispatching of ready queue took %li:%li at %li:%li\n", t3.tv_sec, t3.tv_usec, simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
  }

  /* next try the blocked queue */
  gettimeofday(&t1, NULL);
  pid = bq_pop();
  if(pid > 0){
    ln_check(); printf("OSS: Dispatching process with PID %d from blocked queue at time %li:%li,\n", pid, simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
    nq += scheduler_msg(pid);

    gettimeofday(&t2, NULL);
    timersub(&t2, &t1, &t3);

    /* update time with dispatch duration */
    tincrement(&simulator_obj->clock, &t3);
    ln_check(); printf("OSS: Dispatching of blocked queue took %li:%li at time %li:%li\n", t3.tv_sec, t3.tv_usec, simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
  }

  return nq;
}

/* check number of arguments*/
static int check_arguments(const int argc, char * const argv[]){
  int rtime = TIME_LIMIT;

  int opt;
  while((opt = getopt(argc, argv, "hs:l:")) != -1){
      switch(opt){

        case 's':
          rtime = atoi(optarg);
          if(rtime < 0){
            fprintf(stderr, "Error: Invalid runtime\n");
            return -1;
          }
          break;

        case 'l':
          opt_log = optarg;
          break;

        case 'h':
        default:
          fprintf(stderr, "Usage: ./oss [-h] [-s seconds] [-l logfile.txt]\n");
          return EXIT_FAILURE;
      }
  }

  /* redirect output to log */
  stdout = freopen(opt_log, "w", stdout);
  if(stdout == NULL){
    perror("freopen");
    return -1;
  }

  alarm(rtime);

  return 0;
}

static int scheduler_init(){
  struct msgbuf buf;

  bzero(&buf, sizeof(buf));

  /* insert one execute message, so a process can start */
  buf.mtype = TYPE_EXECUTE;
  if(msg_send(&buf) == -1){
    return -1;
  }

  /* init bit vector and queue */
  bv_init();
  queues_init();

  /* init timers */
  bzero(stat_time, sizeof(stat_time));
  timerclear(&forktime);

  return 0;
}

static int num_procs_exited(){
  return proc_exited[0] + proc_exited[1];
}

/* Do a time jump to next time a process starts */
static int scheduler_tjump(){
  struct timeval tv, idle_from = simulator_obj->clock;
  const struct qitem * item = bq_top();

  /* if we can start another process */
  if(num_procs_exited() < PROC_TOTAL){
    
    if(timercmp(&simulator_obj->clock, &forktime, <)){
      /* advance to next fork time */
      simulator_obj->clock = forktime;
      ln_check(); printf("OSS: Jumped to next fork time %li:%li\n", simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
    }

  /* if we have blocked users */
  }else if(item){

    if(timercmp(&simulator_obj->clock, &item->tv, <)){
      /* move to next unblock time */
      simulator_obj->clock = item->tv;
      ln_check(); printf("OSS: Jumped to next event time %li:%li\n", simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
    }

  /*nobody to fork/unblock */
  }else{
    return -1;
  }

  /* update idle time */
  timersub(&simulator_obj->clock, &idle_from, &tv);
  tincrement(&stat_time[ST_IDLE], &tv);
  if(timerisset(&tv)){
    ln_check(); printf("OSS: idled for %li:%li at %li:%li\n",
      tv.tv_sec, tv.tv_usec,
      simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
  }

  return 0;
}

static void scheduler_tadvance(){
  //advance time
  struct timeval tv, temp = simulator_obj->clock;
  tv.tv_sec += rand() % 2;
  tv.tv_usec += rand() % 1000;
  timeradd(&temp, &tv, &simulator_obj->clock);
  ln_check(); printf("OSS: Advanced time to %li:%li\n", simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
}

static int scheduler_run(){

  /* while we have procs running */
  while(!is_signalled){

    //if its time to start a process
    if(timercmp(&simulator_obj->clock, &forktime, >=)){

      //generate random time, after which a new process will be started
      struct timeval tv;
      tv.tv_sec  = rand() % maxTimeBetweenNewProcsSecs;
      tv.tv_usec = rand() % maxTimeBetweenNewProcsNS;

      tincrement(&forktime, &tv);

      /* check if we can run another user */
      const int num_running = proc_started - num_procs_exited();
      if( (num_running < PROC_LIMIT) &&
          (proc_started < PROC_TOTAL) ){

        /* avoid signals, while we fork and fill proc details */
        block_signals();
        const int rv = docommand();
        unblock_signals();

        if(rv == -1){
          break;
        }else if(rv == 1){
          ++proc_started;
        }
      }
    }

    block_signals();
    const int nq = scheduler_wakeup();
    unblock_signals();

    if(nq == 0){
      /* jump to next fork/unblock time */
      if(scheduler_tjump() < 0){
        break;
      }
    }else{
      /* just update time */
      scheduler_tadvance();
    }

    //sleep(2);
  }

  return 0;
}

static void stat_scheduler(){

  taverage(&stat_time[ST_WAIT], proc_started);
  taverage(&stat_time[ST_EXEC], proc_started);
  taverage(&stat_time[ST_BLOCK0], proc_exited[0]);
  taverage(&stat_time[ST_BLOCK1], proc_exited[1]);

  /* make sure we save stat to log file, even if its after MAX_LINES */
  stdout = freopen(opt_log, "a", stdout);

  printf("Average process exec time: %li:%li\n", stat_time[ST_EXEC].tv_sec, stat_time[ST_EXEC].tv_usec);
  printf("Average ready wait time: %li:%li\n", stat_time[ST_WAIT].tv_sec, stat_time[ST_WAIT].tv_usec);

  printf("Average CPU bound blocked : %li:%li\n", stat_time[ST_BLOCK0].tv_sec, stat_time[ST_BLOCK0].tv_usec);
  printf("Average IO  bound blocked : %li:%li\n", stat_time[ST_BLOCK1].tv_sec, stat_time[ST_BLOCK1].tv_usec);

  printf("CPU idled for : %li:%li\n", stat_time[ST_IDLE].tv_sec, stat_time[ST_IDLE].tv_usec);
  const float cpu_util = (float) stat_time[ST_IDLE].tv_sec / (float)simulator_obj->clock.tv_sec;
  printf("CPU utilization: %.2f%%\n", 100.0f - (cpu_util * 100.0f));
}

int main(const int argc, char * const argv[]){
  struct sigaction sa;

  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = on_interrupt;
  if( (sigaction(SIGINT, &sa, NULL) == -1) ||
      (sigaction(SIGTERM, &sa, NULL) == -1) ||
      (sigaction(SIGCHLD, &sa, NULL) == -1) ||
      (sigaction(SIGALRM, &sa, NULL) == -1)){
     perror("sigaction");
     return EXIT_FAILURE;
  }

  sa.sa_handler = SIG_IGN;
  if(sigaction(SIGTERM, &sa, NULL) == -1){
     perror("sigaction");
     return EXIT_FAILURE;
  }

  if(check_arguments(argc, argv) < 0){
    return EXIT_FAILURE;
  }

  /* create the error string from program name */
  snprintf(perror_buf, sizeof(perror_buf), "%s: Error: ", argv[0]);

  /* create the license object */
  if(create_simulator(1) < 0){
    return EXIT_FAILURE;
  }

  if(scheduler_init() < 0){
    return EXIT_FAILURE;
  }

  scheduler_run();

  /* wait for any processes left */
  while(num_procs_exited() < proc_started){
    do_wait(0);
  }

  printf("OSS: master terminated at %li:%li.\n", simulator_obj->clock.tv_sec, simulator_obj->clock.tv_usec);
  stat_scheduler();

  destroy_simulator(1);

  return EXIT_SUCCESS;
}
