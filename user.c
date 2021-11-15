#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <strings.h>

#include "config.h"
#include "common.h"

int main(const int argc, char * argv[]){
  int my_id;
  struct msgbuf buf;
  struct proc * proc;

  /* convert arguemnts to int */
  my_id  = atoi(argv[1]);

  /* initialize the random generator */
  srand(my_id + time(NULL));

  /* create the error string from program name */
  snprintf(perror_buf, sizeof(perror_buf), "%s: Error: ", argv[0]);

  /* attach to the license object */
  if(create_simulator(0) < 0){
    return EXIT_FAILURE;
  }

  /* get our control block */
  proc = &simulator_obj->procs[my_id];
  proc->action = ACT_EXEC;

  while(proc->action != ACT_TERM){ //while we haven't decided to terminate

    if(is_signalled){
      break;
    }

    /* receive message from master */
    buf.mtype = getpid();
    if(msg_recv(&buf)  == -1){
      break;
    }

    if(!timerisset(&buf.slice)){ //if time slice is 0
      break;  //stop
    }

    /* terminate ?*/
    if((rand() % 100) < CHANCE_TO_TERMINATE){

      /* use part of allocated slice */
      proc->timer[T_BURST].tv_usec = rand() % buf.slice.tv_usec;
      proc->action = ACT_TERM;

    }else{

      /* interrupt for IO ?*/
      if((rand() % 100) < interrupt_prob[proc->bound]){

        proc->action = ACT_INT;

        /* use part of allocated slice */
        proc->timer[T_BURST].tv_usec = rand() % buf.slice.tv_usec;

        /* IO duration */
        proc->timer[T_IOEND].tv_sec  = rand() % 6;     //[0, 5]
        proc->timer[T_IOEND].tv_usec = rand() % 1001;  //[0, 1000]

      }else{
        /* execute */
        proc->action = ACT_EXEC;
        /* process will execute for whole timeslice */
        proc->timer[T_BURST] = buf.slice;
      }
    }

    bzero(&buf, sizeof(buf));

    /* send message to oss, to inform our burst is over */
    buf.mtype = TYPE_BURSTED;
    /* save our ID in message */
    buf.id = my_id;
    if(msg_send(&buf) == -1){
      break;
    }
  }

  destroy_simulator(0);

  return EXIT_SUCCESS;
}
