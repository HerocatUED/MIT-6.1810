// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmem_pool[NCPU];

void
kinit()
{
  for(int i=0; i<NCPU; i++){
    char lock_names[9];
    snprintf(lock_names, 9, "kmemCPU%d", i);
    initlock(&kmem_pool[i].lock, lock_names);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int id = cpuid();
  acquire(&kmem_pool[id].lock);
  r->next = kmem_pool[id].freelist;
  kmem_pool[id].freelist = r;
  release(&kmem_pool[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();
  acquire(&kmem_pool[id].lock);
  r = kmem_pool[id].freelist;
  if(r)
    kmem_pool[id].freelist = r->next;
  else{// free list is empty, "steal" part of the other CPU's free list
    for(int i=0; i<NCPU;  i++){
      if(i==id) continue;
      acquire(&kmem_pool[i].lock);
      r = kmem_pool[i].freelist;
      if(r){
        kmem_pool[i].freelist = r->next;
        release(&kmem_pool[i].lock);
        break;
      }
      release(&kmem_pool[i].lock);
    }

  }
  release(&kmem_pool[id].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
