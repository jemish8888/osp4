#ifndef COMMON_H
#define COMMON_H

#include <sys/time.h>
#include <sys/types.h>
#include "config.h"

/* actions a process can take - execute, terminate, interrupt */
enum proc_action {ACT_EXEC, ACT_TERM, ACT_INT};
/* a process can be bound to CPU or IO */
enum proc_bound {B_CPU=0, B_IO};

/* times a process has in its control block - start, execute, wait, blocked */
enum proc_timer {T_START=0, T_EXEC, T_WAIT, T_BLOCKED, T_BURST, T_IOEND, T_COUNT};

struct proc {
  pid_t pid;
  int id;
  enum proc_bound bound;
  struct timeval timer[T_COUNT];
  /* what was last proc action */
  enum proc_action action;
};

struct simulator_object {
  struct timeval clock;
  struct proc procs[PROC_LIMIT];
};

enum msg_types {
  //can't start at 0, because its invalid message type

  TYPE_EXECUTE=1, //users wait for this to run (oss -> user)
  TYPE_BURSTED,   //users send this, when they have completed burst (user -> oss)
};

struct msgbuf {
 long mtype;       /* message type, must be > 0 */
 int id;
 struct timeval slice;
};

/* perror prefix */
extern char perror_buf[100];
/* signal handler flag for interruption */
extern int is_signalled;

/* Simulator object pointer to shared memory */
extern struct simulator_object * simulator_obj;

/* create license object*/
int create_simulator(const int n);
/* destroy and cler the shared memory object */
int destroy_simulator();

int msg_send(const struct msgbuf * buf);
int msg_recv(      struct msgbuf * buf);

/* helper functions */

/* find process id by its pid */
int find_id(pid_t pid);

/* Find process control block by PID */
struct proc * find_proc(const pid_t pid);

/* increment a timer */
void tincrement(struct timeval * t, const struct timeval * inc);
void taverage(struct timeval * t, const unsigned int x);

#endif
