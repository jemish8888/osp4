#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <strings.h>
#include <time.h>

#include "config.h"
#include "common.h"

static int shmid = -1, msgid = -1;
static char simulator_file[PATH_MAX];

int is_signalled = 0;
struct simulator_object * simulator_obj = NULL;
char perror_buf[100];

int create_simulator(const int num_licenses){

  /* create the license filename, using user ID*/
  snprintf(simulator_file, PATH_MAX, "/tmp/oss_simulator.%u", getuid());

  if(num_licenses){
    printf("OSS: Simulator file is %s\n", simulator_file);

    /* create the license file */
    int fd = creat(simulator_file, 0700);
    if(fd == -1){
      perror(perror_buf);
      return -1;
    }
    close(fd);
  }

  //create key for shared memory
  key_t license_key = ftok(simulator_file, 4444);
  if(license_key == -1){
    perror(perror_buf);
    return -1;
  }

  shmid = shmget(license_key, sizeof(struct simulator_object), (num_licenses) ? IPC_CREAT | IPC_EXCL | S_IRWXU : 0);
  if(shmid == -1){
    perror(perror_buf);
    return -1;
  }

  //create key for message queue
  license_key = ftok(simulator_file, 5555);
  if(license_key == -1){
    perror(perror_buf);
    return -1;
  }

  //get the message queue
  msgid = msgget(license_key, (num_licenses) ? IPC_CREAT | IPC_EXCL | S_IRWXU : 0);
  if(msgid == -1){
    perror(perror_buf);
    return -1;
  }

  simulator_obj = (struct simulator_object*) shmat(shmid, NULL, 0);
  if(simulator_obj == (void*)-1){
    perror(perror_buf);
    return -1;
  }

  if(num_licenses){
    /* clear the license object */
    bzero(simulator_obj, sizeof(struct simulator_object));
  }

  return 0;
}

int destroy_simulator(const int num_licenses){
  int rv = 0;

  if(shmdt(simulator_obj) == -1){
    perror(perror_buf);
    return -1;
  }

  if(num_licenses > 0){
    printf("OSS: Destroying simulator file %s\n", simulator_file);

	  if(shmctl(shmid, IPC_RMID, NULL) == -1){
      perror(perror_buf);
      rv = -1;
    }

    if(msgctl(msgid, IPC_RMID, NULL) == -1){
      perror(perror_buf);
      rv = -1;
    }

    if(unlink(simulator_file) == -1){
      perror(perror_buf);
      rv = -1;
    }
  }
  return rv;
}

int msg_send(const struct msgbuf * buf){
  if(msgsnd(msgid, buf, sizeof(int) + sizeof(struct timeval), 0) == -1){
    perror(perror_buf);
    return -1;
  }
  return 0;
}

int msg_recv(struct msgbuf * buf){
  if (msgrcv(msgid, buf, sizeof(int) + sizeof(struct timeval), buf->mtype, 0) == -1){
    perror(perror_buf);
    return -1;
  }
  return 0;
}

int find_id(pid_t pid){
  unsigned int i;
  for(i=0; i < PROC_LIMIT; i++){
    if(simulator_obj->procs[i].pid == pid){
      return i;
    }
  }
  return -1;
}

struct proc * find_proc(const pid_t pid){
  unsigned int i;
  for(i=0; i < PROC_LIMIT; i++){
    if(simulator_obj->procs[i].pid == pid){
      return &simulator_obj->procs[i];
    }
  }
  return NULL;
}

//Increment a timer
void tincrement(struct timeval * t, const struct timeval * inc){
  struct timeval tv = *t;
  timeradd(&tv, inc, t);  //add increment to t
}

//Divide a timer
void taverage(struct timeval * t, const unsigned int x){
  if(t->tv_sec != 0){   t->tv_sec  /= x;  }
  if(t->tv_usec != 0){  t->tv_usec /= x;  }
}
