#include <stddef.h>
#include "types.h"
#include "mmu.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "fs.h"
#include "file.h"

struct mmap_area mmap_area_list[64] = {
  [0 ... 63] = { .f = NULL, .addr = 0, .length = 0, .offset = 0, .prot = 0, .flags = 0, .p = NULL, .mark = 'e' }
};

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

void allocate_mmap_area_list_member(
  int idx, struct file* file_to_use, 
  unsigned int start_addr, int length,
  int offset, int prot, int flags, char mark,
  struct proc* p
) {
  if (idx >= 64){
    cprintf("Wrong idx!\n");
  }
  struct mmap_area *entry = &mmap_area_list[idx];

  entry->f = file_to_use;

  if ((start_addr % 4096)){
    cprintf("Page not aligned for start addr\n");
  }
  entry->addr = start_addr;

  if ((length % 4096)){
    cprintf("Page not aligned length\n");
  }
  entry->length = length;
  
  if (offset<0){
    cprintf("Wrong option for offset\n");
  }
  entry->offset = offset;
  entry->prot = prot;
  entry->flags = flags;

  if (!(mark == 'e' || mark == 'n' || mark == 'a')){
    cprintf("Wrong mark condition!\n");
  }
  entry->mark = mark;
  entry->p = p;
}


int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();
  int return_value = 0;

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
  
  int parent_idx=0;
  int parent_idx_list[64];

  // Find every parent idx
  for (int i=0; i<64; i++){
    parent_idx_list[i] = -1;
  }

  i = 0;

  for(parent_idx=0; parent_idx<64; parent_idx++){
    if(
      mmap_area_list[parent_idx].mark == 'n' ||
      mmap_area_list[parent_idx].mark == 'a'
    ){
      if(mmap_area_list[parent_idx].p == curproc){
        // Found our parent process!
        // Append this to parent idx list group
        parent_idx_list[i] = parent_idx;
        i++;
      }
    }
  }
  if (i){//If there is more than 1 membver for parent_idx_list
    for (i=0; i<64; i++){//Let's go
      parent_idx = parent_idx_list[i];
      if (parent_idx == -1){
        break;
      }

      int is_done = 0;
      for(int child_idx=0; child_idx<64; child_idx++){
        if(child_idx != parent_idx){
          if((mmap_area_list[child_idx].mark == 'e')&&!is_done){//Find empty slot
            struct mmap_area *parent_area = &mmap_area_list[parent_idx];
            allocate_mmap_area_list_member(//Input the slot
              child_idx,
              parent_area->f,
              parent_area->addr,
              parent_area->length,
              parent_area->offset,
              parent_area->prot,
              parent_area->flags,
              parent_area->mark,
              np
            ); // Copy parent's mmap area
            char is_file_mapping = parent_area->flags%2==0;
            char is_already_allocated = (parent_area->mark=='a');

            if (is_already_allocated){
              int page_size = 4096;

              char *memory_pointer = NULL;
              
              for(
                int count=0; 
                count<(int)(parent_area->length/page_size); 
                count++
              ){
                if(!(memory_pointer = kalloc())){
                  return return_value;
                }
                else{
                  memset(memory_pointer, 0, page_size);

                  if (is_file_mapping){
                    struct file* file_to_use = parent_area->f;
                    file_to_use->off = parent_area->offset;

                    file_to_use->ref += 1;
                    fileread(
                      file_to_use, memory_pointer, page_size              
                    );
                    file_to_use->ref -= 1;
                  }

                  char is_usermode = 1;
                  int page_mapping_success = mappages(
                    np->pgdir,
                    (void*)(
                      parent_area->addr + count*page_size
                    ),
                    page_size, V2P(memory_pointer), parent_area->prot,
                    is_usermode
                  );

                  if (!(page_mapping_success+1)) return return_value;
                }
              }
            }
            is_done=1;
          }
        }
      }
    }
  }
  

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

unsigned int mmap(
  unsigned int addr, int length,
  int prot, int flags, int fd, int offset
){
  struct file *file_to_use = NULL;
  char is_file_mapping = 1;
  if (fd == -1 && offset == 0) {
    is_file_mapping = 0;
  }
  unsigned int return_value = 0; // fail
  
  unsigned int start_addr = MMAPBASE+addr;
  if (is_file_mapping) {
    file_to_use = myproc()->ofile[fd];
  }

  if (prot == PROT_READ){
    // fail handler
    if (flags%2){ // anonmous mapping
      if (is_file_mapping){
        //cprintf("wrong1\n");
        return return_value;
      }
    }
    else { // flags == MAP_POPULATE, file mapping
      if (
        file_to_use->type != 2 ||
        !is_file_mapping ||
        file_to_use->readable == '0'
      ){
        //cprintf("wrong2\n");
        return return_value;
      } // It's not anonymous, but when the fd is -1
    }
  }
  else { // prot == PROT_WRITE
    if (flags%2){ // anonmous mapping
      if (is_file_mapping){
        //cprintf("wrong3\n");
        return return_value;
      }
    }
    else { // flags == MAP_POPULATE, file mapping
      if (
        file_to_use->type != 2 ||
        !is_file_mapping ||
        file_to_use->writable == '0'
      ){
        //cprintf("wrong4\n");
        return return_value;
      } // It's not anonymous, but when the fd is -1
    }
  }
  //cprintf("Wrong check passed\n");

  int idx = 0;
  char is_done = 0;

  int found_idx = 64;
  for (idx=0; idx< 64; idx++){
    if (mmap_area_list[idx].mark == 'e' && !is_done) {
      if (is_file_mapping){
        //cprintf("filed up\n");
        file_to_use = filedup(file_to_use);
      }
      allocate_mmap_area_list_member(
        idx, file_to_use, start_addr,
        length, offset, prot, flags, 'n', myproc()
      );
      //cprintf("Mmap allocated to idx: %d\n", idx);
      is_done=1;
      found_idx=idx;
    }
  }
  if(found_idx==64) cprintf("No empty slot!");
  idx=found_idx;

  // mmap 마지막에 넣었으니 이제 시작
  // !Populate인데 file=mapped | anonymous 인 경우
  // 그냥 주소 return -> why? 나중에 page fault 일어나면 handle

  if (!(flags/2)){
    //cprintf("Not populate case\n");
    return start_addr; // not populate
  }
  // 2 cases : 1. Populate & Anonymous 2. Populate & FileMapped
    
  //cprintf("Populate case\n");

  // 1. Anonymous case
  int page_size = 4096;

  char *memory_pointer = NULL;
  file_to_use->off = offset;
  
  //cprintf("Starting allocate populate case\n");

  for(
    int count=0; 
    count<(int)(length/page_size); 
    count++
  ){
    //cprintf("Allocate: %d번째 case\n", count);

    //cprintf("KALLOC\n");
    if(!(memory_pointer = kalloc())){
      return return_value;
    }
    else{
      memset(memory_pointer, 0, page_size);

      //cprintf("MEMSET\n");

      if (is_file_mapping){
        file_to_use->ref += 1;
        fileread(
          file_to_use, memory_pointer, page_size
        );
        file_to_use->ref -= 1;
      }

      char is_usermode = 1;
      int page_mapping_success = mappages(
        myproc()->pgdir,
        (void*)(
          start_addr + count*page_size
        ),
        page_size, V2P(memory_pointer), prot,
        is_usermode
      );

      if (!(page_mapping_success+1)) return return_value;
    }
  }
  allocate_mmap_area_list_member(
      idx, file_to_use, start_addr,
      length, offset, prot, flags, 'a', myproc()
  ); // Allocate instantly so mark with 'a'
  return start_addr;
}

int munmap(unsigned int addr){
  int return_value = -1;
  int temp = 0;
  int idx = 0;
  int page_size = 4096;

  if (addr%page_size) return return_value;

  int found_idx = 0;
  char is_done = 0;

  for(
    temp=0;
    temp<64;
    temp++
  )
    if(
      mmap_area_list[temp].addr == addr
      && mmap_area_list[temp].p == myproc()
      && !is_done
    ){
      is_done = 1;
      found_idx = temp;
      break;
    }
  if (temp < 64){
    idx = found_idx;
    if(mmap_area_list[idx].mark=='n'){
        mmap_area_list[idx].mark = 'e';
      return 1;
    }
  }
  else{
    return return_value;
  }

  for(
    int count=0; 
    count<(int)(mmap_area_list[idx].length/page_size); 
    count++
  ){
    unsigned int *page_table_entry = walkpgdir(
      myproc()->pgdir,
      (char *)(addr+count*page_size), 0
    );
    if((*page_table_entry%2)){
      if (page_table_entry){
        kfree(P2V(PTE_ADDR(*page_table_entry)));
        *page_table_entry = !(*page_table_entry%2);
      }
      else return return_value;
    }
    else{
      continue;
    }
  }

  mmap_area_list[idx].mark = 'e';
  return 1;
}

int freemem(void){
  return get_free_count();
}

int pagefault_handle(
  unsigned int address, 
  unsigned int err_2place_bit,
  struct proc* curproc
){
  int return_value = -1;
  int temp = 0;
  int idx = 0;
  int page_size = 4096;

  char is_done = 0;
  int found_idx = 64;

  for(
    temp=0;
    temp<64;
    temp++
  ){
    if(
      mmap_area_list[temp].addr <= address &&
      (mmap_area_list[temp].p==curproc) &&
      (
        address - mmap_area_list[temp].addr<
        mmap_area_list[temp].length
      ) &&
      !is_done
    ){
      is_done = 1;
      found_idx = temp;
    }
  }
  temp = found_idx;

  //cprintf("Page handler found idx: %d\n", temp);

  if(!(temp < 64)){
    return return_value;
  }
  else{
    idx = temp;
  }
  
  if(
    (
      mmap_area_list[idx].prot != 3
      && err_2place_bit
    ) 
  ) return return_value;

  if(
    mmap_area_list[idx].mark == 'n'
  ){
    char *memory_pointer = NULL;
    char is_file_mapping = mmap_area_list[idx].flags%2==0;

    struct file* file_to_use = mmap_area_list[idx].f;
    file_to_use->off = mmap_area_list[idx].offset;
    
    for(
      int count=0; 
      count<(int)(mmap_area_list[idx].length/page_size); 
      count++
    ){

      if(!(memory_pointer = kalloc())){
        return return_value;
      }
      else{
        memset(memory_pointer, 0, page_size);

        if (is_file_mapping){
          file_to_use->ref += 1;
          fileread(
            file_to_use, memory_pointer, page_size
          );
          file_to_use->ref -= 1;
        }

        char is_usermode = 1;
        int page_mapping_success = mappages(
          curproc->pgdir,
          (void*)(
            mmap_area_list[idx].addr + count*page_size
          ),
          page_size, V2P(memory_pointer),
          mmap_area_list[idx].prot,
          is_usermode
        );

        if (!(page_mapping_success+1)) return return_value; 
      }
    }
    allocate_mmap_area_list_member(
        idx, file_to_use, mmap_area_list[idx].addr,
        mmap_area_list[idx].length, mmap_area_list[idx].offset,
        mmap_area_list[idx].prot, mmap_area_list[idx].flags, 'a',
        mmap_area_list[idx].p
    ); // Allocate instantly so mark with 'a'
    return 0;
  }
  //cprintf("Invalid operation!");
  return -1;
}
