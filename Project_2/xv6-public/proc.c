#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

int weight_table[40] = 
{
  88761,  71755,  56483,  46273,  36291,
  29154,  23254,  18705,  14949,  11916,
  9548,   7620,   6100,   4904,   3906,
  3121,   2501,   1991,   1586,   1277,
  1024,   820,    655,    526,    423,
  335,    272,    215,    172,    137,
  110,    87,     70,     56,     45,
  36,     29,     23,     18,     15
};

unsigned int total_weight = 0;

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->nice = 20;

  p->cur_runtime = 0;
  p->runtime = 0;
  p->vruntime = 0;
  p->time_slice = 0; // 0 for now

  p->overflow_times = 0;
  

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;

  np->nice = curproc->nice;
  np->overflow_times = curproc->overflow_times;
  np->vruntime = curproc->vruntime;

  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    struct proc *min_vrun_proc = 0;
    int is_there_min_proc = 0;
    unsigned int min_vruntime = (unsigned int) -1;
    unsigned int min_overflow_times = (unsigned int) -1;

    acquire(&ptable.lock);

    total_weight = 0;

    // Get total weight of the process
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == RUNNABLE)
        total_weight += weight_table[p->nice];
    }

    // Set time slice for every process
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == RUNNABLE)
         p->time_slice = 1000*10*weight_table[p->nice]/total_weight;
    }

    // choose process to run(with min vruntime)
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      if (p->state == RUNNABLE){
        if (p->vruntime == 0 && p->overflow_times == 0)
        {
          is_there_min_proc = 1;
          min_vrun_proc = p;
          break;
        }
        if (
          p->overflow_times < min_overflow_times ||
          (p->vruntime < min_vruntime && p->overflow_times == min_overflow_times)
        )
        {
          is_there_min_proc = 1;
          min_vruntime = p->vruntime;
          min_overflow_times = p->overflow_times;
          min_vrun_proc = p;
        }
      }

    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    if (!is_there_min_proc){
      release(&ptable.lock);
      continue;
    }

    c->proc = min_vrun_proc;
    min_vrun_proc->state = RUNNING;
    switchuvm(min_vrun_proc);

    swtch(&(c->scheduler), min_vrun_proc->context);
    switchkvm();

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock"); // panic: 치명적 요류 떴을때라 함
  if(mycpu()->ncli != 1) // ncli: 인터럽트가 비활성화된 횟수
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  // 고친부분
  
  struct proc *min_vrun_proc = 0;
  int is_there_runable = 0; // runable 존재하는지
  unsigned int min_vruntime = (unsigned int) -1;
  unsigned int min_overflow_times = (unsigned int) -1;

  // choose process min process(with min vruntime)
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == RUNNABLE){
      if (p->vruntime == 0 && p->overflow_times == 0)
      {
        is_there_runable = 1;
        min_vrun_proc = p;
        break;
      }
      if (
        p->overflow_times < min_overflow_times ||
        (p->vruntime < min_vruntime && p->overflow_times == min_overflow_times)
      )
      {
        is_there_runable = 1;
        min_vruntime = p->vruntime;
        min_overflow_times = p->overflow_times;
        min_vrun_proc = p;
      }
    }


  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
    {
      p->state = RUNNABLE;
      if(is_there_runable)
      {
        unsigned int one_tick_minus = 1000*1024/weight_table[myproc()->nice];
        
        if(min_vrun_proc->vruntime >= one_tick_minus)
        {
          p->overflow_times = min_vrun_proc->overflow_times;
          p->vruntime = min_vrun_proc->vruntime - one_tick_minus;
        }
        else if(min_vrun_proc->overflow_times)
        {
          p->overflow_times = min_vrun_proc->overflow_times - 1;
          p->vruntime = ((unsigned int) -1) - one_tick_minus + min_vrun_proc->vruntime; 
        }
        else 
        {
          p->overflow_times = 0;
          p->vruntime = 0;
        }
      }
      else
      {
        p->overflow_times = 0;
        p->vruntime = 0;
      }
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  
  struct proc *min_vrun_proc = 0;
  int is_there_runable = 0; // runable 존재하는지
  unsigned int min_vruntime = (unsigned int) -1;
  unsigned int min_overflow_times = (unsigned int) -1;

  // choose process min process(with min vruntime)
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == RUNNABLE){
      if (p->vruntime == 0 && p->overflow_times == 0)
      {
        is_there_runable = 1;
        min_vrun_proc = p;
        break;
      }
      if (
        p->overflow_times < min_overflow_times ||
        (p->vruntime < min_vruntime && p->overflow_times == min_overflow_times)
      )
      {
        is_there_runable = 1;
        min_vruntime = p->vruntime;
        min_overflow_times = p->overflow_times;
        min_vrun_proc = p;
      }
    }

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
      {
        p->state = RUNNABLE;
        if(is_there_runable)
        {
          unsigned int one_tick_minus = 1000*1024/weight_table[myproc()->nice];

          if(min_vrun_proc->vruntime >= one_tick_minus)
          {
            p->overflow_times = min_vrun_proc->overflow_times;
            p->vruntime = min_vrun_proc->vruntime - one_tick_minus;
          }
          else if(min_vrun_proc->overflow_times)
          {
            p->overflow_times = min_vrun_proc->overflow_times - 1;
            p->vruntime = ((unsigned int) -1) - one_tick_minus + min_vrun_proc->vruntime; 
          }
          else
          {
            p->overflow_times = 0;
            p->vruntime = 0;
          }
        }
        else
        {
          p->overflow_times = 0;
          p->vruntime = 0;
        }
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
getnice(int pid)
{
  struct proc *p;
  acquire(&ptable.lock);

  int return_value = -1;
  for(
    p = ptable.proc;
    p < &ptable.proc[NPROC];
    p++
  ){
    if(
      p->pid == pid
    ){
      return_value = p->nice;
      break;
    }
  }
  release(&ptable.lock);
  return return_value;
}

int
setnice(int pid, int value)
{
  int return_value = -1;
  if(value > 39 || value < 0){
    return return_value;
  }

  struct proc *p;
  acquire(&ptable.lock);
  for(
    p = ptable.proc;
    p < &ptable.proc[NPROC];
    p++
  ){
    if(p->pid == pid){
      p->nice = value;

      return_value = 0;
  
      break;
    }
  }
  release(&ptable.lock);
  return return_value;
}

void itoiarr(unsigned int num, int *iarr) {
    int i = 0;
    do {
        iarr[i] = (num % 10);
        i += 1;
        num = num / 10;
    } while (
      num > 0
    );

    for (int j = i; j<20; j++) 
    {
      iarr[j] = 0;
    }
    
}

void print_int(
  int num,
  int max_len
)
{
  cprintf("%d", num);
  
  int num_len = 0;
  if (num)
  {
    while (num != 0) {
      num = num / 10;
      num_len++;
    }
  }
  else num_len = 1;
  
  for(int left=0; left < max_len - num_len; left++)
  { 
    cprintf(" ");
  }
}

void print_unsigned(
  unsigned int num,
  int max_len
)
{
  cprintf("%d", num);
  
  int num_len = 0;
  if (num)
  {
    while (num != 0) {
      num = num / 10;
      num_len++;
    }
  }
  else num_len = 1;
  
  for(int left=0; left < max_len - num_len; left++)
  { 
    cprintf(" ");
  }
}

void print_string(
  char* string,
  int max_len
)
{
  cprintf("%s", string);

  int idx = 0;

  while (string[idx] != '\0') {
    idx++;
  }
  
  for(int left=0; left < max_len - idx; left++)
  { 
    cprintf(" ");
  }
}

void print_vruntime(
  unsigned int overflow_num,
  unsigned int vruntime_num
){
  if (overflow_num){
    int output_number[20] = {0};

    int carry = 0;
    // overflow part
    for (int overflow_idx = 0; overflow_idx<overflow_num; overflow_idx++){
      int to_iarr[20];
      unsigned int overflowed = (unsigned int) -1;
      itoiarr(overflowed, to_iarr);

      for (int idx = 0; idx < 20; idx++){
        int result = to_iarr[idx] + output_number[idx] + carry;
        output_number[idx] = (
          result
        ) % 10;
        if (result >= 10) carry = 1;
        else carry = 0;
      }
    }
    // Leftover part
    int to_iarr[20];
    itoiarr(vruntime_num, to_iarr);

    carry = 0;

    for (int idx = 0; idx < 20; idx++){
      int result = to_iarr[idx] + output_number[idx] + carry;
      output_number[idx] = (
        result
      ) % 10;
      if (result >= 10) carry = 1;
      else carry = 0;
    }

    int start = 0;

    for (int idx = 19; idx >= 0; idx--){
      if (!start && output_number[idx] != 0){
        start = 1;
      }
      if (start){
        cprintf("%d", output_number[idx]);
      }
    }
  }
  else{
    cprintf("%d", vruntime_num);
  }
  cprintf("\n");
}


void ps(int pid){
  char *states_by_idx[] = {"UNUSED", "EMBRYO", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE"};
  struct proc *p;

  acquire(&ptable.lock);
  print_string("name", 10);
  print_string("pid", 10);
  print_string("state", 20);
  print_string("priority", 10);
  print_string("runtime/weight", 20);
  print_string("runtime", 20);
  print_string("vruntime", 20);
  cprintf("tick %d\n", ticks*1000);

  if(pid){
    for(
      p = ptable.proc;
      p <= &ptable.proc[NPROC];
      p++
    ){
      if(p->pid == pid){
        if(p->state != 0 && p->state <= 5){
          print_string(p->name, 10);
          print_int(p->pid, 10);
          print_string(states_by_idx[p->state], 20);
          print_int(p->nice, 20);
          print_unsigned(p->runtime/weight_table[p->nice], 20);
          print_unsigned(p->runtime, 20);
          print_vruntime(p->overflow_times, p->vruntime);
        }
        break;
      }
    }
  }
  else{
    for(
      p = ptable.proc;
      p <= &ptable.proc[NPROC];
      p++
    ){
      if(p->state != 0 && p->state <= 5 && p->pid >= 0){
        print_string(p->name, 10);
        print_int(p->pid, 10);
        print_string(states_by_idx[p->state], 20);
        print_int(p->nice, 10);
        print_unsigned(p->runtime/weight_table[p->nice], 20);
        print_unsigned(p->runtime, 20);
        print_vruntime(p->overflow_times, p->vruntime);
      }
    }
  }
  
  release(&ptable.lock);
}
