// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld
char parity_check(void);
void swap_out(struct page*);

struct spinlock paging_lock;
struct spinlock clock_algorithm_lock;
int pages_valid_bits[PHYSTOP/4096];

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
int num_free_pages;
int num_lru_pages;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");
  else{
    num_free_pages++;
  }

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
  while(1){
    if(kmem.use_lock)
        acquire(&kmem.lock);
    if(!(r = kmem.freelist))
    {
        if(!kmem.use_lock){
            if(!parity_check()) 
            {
                panic("OOM\n"); return 0;
            }
        }
        else{
            release(&kmem.lock);
        }
    }
    else break;
  }
  num_free_pages--;
  kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}

void pagelist_insertion(
    char* virtual_addr, int success, unsigned int *page_dir
)
{
    acquire(&clock_algorithm_lock);

    struct page* pge = 0;

    int idx = 0;

    for(
        idx=0;
        idx<0xE000000/0x1000;
        idx++
    )
        if(!pages[idx].pgdir){
            success = 1;
            break;
        }
    
    if (success) pge = &pages[idx];

    if(pge){
        pge->pgdir=page_dir;
        pge->vaddr=virtual_addr;
        
        if(num_lru_pages < 0) cprintf("Wrong num lru condition!");
        else if(num_lru_pages/2){
            pge->next = page_lru_head;
            pge->prev = page_lru_head->prev;

            page_lru_head->prev->next=pge;
            page_lru_head->prev = pge;
        }
        else if(num_lru_pages%2){
            pge->next=page_lru_head;
            pge->prev=page_lru_head;

            page_lru_head->prev=pge;
            page_lru_head->next=pge;
        }
        else{
            page_lru_head=pge;

            page_lru_head->next=pge;
            page_lru_head->prev=pge;
        }
        release(&clock_algorithm_lock);
    }
    else{
        cprintf("Page array does not have free space");
    }
}

char page_list_remove(
    char* virtual_addr, int success, unsigned int *page_dir
)
{
    char default_return_value = 'n';

    acquire(&clock_algorithm_lock);
    unsigned int* page_table_entry= walkpgdir(
        page_dir, virtual_addr, 0
    );

    if(num_lru_pages){
        if((*page_table_entry&0x100) >= 0x100) {
            struct page* pge = page_lru_head;
            while(1)
            {
                if(
                    pge->vaddr == virtual_addr &&
                    pge->pgdir == page_dir
                ){
                    if(pge!=page_lru_head){
                        pge->next->prev=pge->prev;
                        pge->prev->next=pge->next;
                        
                        num_lru_pages -= 1;
                        break;
                    }
                    else{
                        page_lru_head=pge->next;
                    }
                }
                struct page* before = pge;
                pge=pge->next;
                if(before == pge){
                    break;
                }
            }
        }
        else
        {
            pages_valid_bits[
                *page_table_entry/4096
            ]=num_lru_pages;
            success = 1;
        }
    }

    release(&clock_algorithm_lock);
    if(!success) return default_return_value;
    else return 'e';
}

void page_fault_handle(
    unsigned int trap_no, unsigned int faddress, unsigned int *page_dir
)
{
    if(trap_no!=14) panic("Wrong trap condition!");
    unsigned int* fault_entry = walkpgdir(
        page_dir, (void*)faddress, 0
    );

    if(*fault_entry&0x100){
        char parity_check_value = parity_check();
        
        if(
            !parity_check_value && !num_free_pages
        ) panic("parity_check failed\n");
        
        char* allocated_memory = kalloc();
        
        if(allocated_memory){ // success
            pages_valid_bits[*fault_entry / PGSIZE] = 0;
            
            memset(allocated_memory, 0, 4096);

            *fault_entry = V2P(allocated_memory) |
                (*fault_entry % PGSIZE - 0b11111111111);

            pagelist_insertion((char*)faddress, 0, page_dir);
            swapread(allocated_memory,*fault_entry/PGSIZE);
        }
    }
}

char parity_check()
{
    char return_value = 0;
    char success = 1;

    struct page* position = page_lru_head;
    if(!num_lru_pages) return return_value;

go_on:
{
    unsigned int* pte = walkpgdir(
        position->pgdir,
        (void*)position->vaddr,
        return_value
    );
    if((!(*pte&0x020)) && ((*pte&0x004))) 
    {
        swap_out(position);
        return success;
    }
    if(page_lru_head != position->next){
        position=position->next;
        goto go_on;
    }
    else{
        goto end_option;
    }
}
end_option:
{
    position=page_lru_head;
    char found=0;

    int page_num;
    char is_done = 0;

    for(
        page_num=0;
        page_num<num_lru_pages;
        page_num++
    )
    {
        if(!is_done){
            if(
                (*walkpgdir(
                    position->pgdir, (void*)position->vaddr, (int) return_value
                )
                &0x004)
            ){
                is_done = 1;
                found=1;
            }
            position=position->next;
        }
    }
    char value_to_return;
    if(!found){
        value_to_return = return_value;
    } 
    else{
        swap_out(position);
        value_to_return = success;
    }
    return value_to_return;
    }
}


void swap_send(
    unsigned int * page_dir, int next_offset, unsigned int * next_pgdir, int idx
)
{
    char done = 0;
    int j_idx;

    unsigned int* page_table_entry = walkpgdir(
        page_dir,
        (void*)idx,
        0
    );
    int before_offset = *page_table_entry/PGSIZE;

    for(j_idx=0; j_idx<PHYSTOP/4096; j_idx++)
    {   
        if (!j_idx) continue;
        if(!pages_valid_bits[j_idx] && !done)
        {
            next_offset=j_idx;
            done = 1;
        }
    }
    mov_buff(before_offset,SWAPMAX,next_offset);

    unsigned int* next_entry = walkpgdir(next_pgdir, (void*) idx, 0);
    *next_entry= (*page_table_entry%PGSIZE)+(next_offset*PGSIZE);
}

void swap_out(struct page* pages_to_out)
{  
    int out_offset=0;
    char done = 0;
    int idx = 0;

    acquire(&paging_lock);

    unsigned int* pte = walkpgdir(
        pages_to_out->pgdir,
        (void*)pages_to_out->vaddr, 0
    );

    for(
        idx=1;
        idx<PHYSTOP/PGSIZE;
        idx++
    ){
        if (!idx) continue;
        if(pages_valid_bits[idx]==0 && !done)
        {
            out_offset=idx;
            done=1;
        }
    }
    
    pages_valid_bits[out_offset]=1;
    
    page_lru_head=pages_to_out->next;
    num_lru_pages--;


    pages_to_out->prev->next=pages_to_out->next;
    pages_to_out->next->prev=pages_to_out->prev;


    swapwrite((char*)P2V(
        PTE_ADDR((out_offset*PGSIZE)| 0x100 |(*pte%PGSIZE-1))
    ),out_offset);
    kfree((char*)P2V(PTE_ADDR(
        (out_offset*PGSIZE)| 0x100 |(*pte%PGSIZE-1)
    )));

    release(&paging_lock);

}

