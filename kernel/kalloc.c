// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// 实现cow, 需要维护引用计数信息, 只有当物理页的引用计数减为 0 时才能删除该页
#define PA2PGREF_ID(pa) ((pa-KERNBASE)/PGSIZE)
#define MAX_PGREF_LENGTH (PA2PGREF_ID(PHYSTOP))
static int page_ref[MAX_PGREF_LENGTH];
#define PA2PGREF(pa) page_ref[PA2PGREF_ID(pa)]
struct spinlock page_ref_lock;

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&page_ref_lock, "page_ref");
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

  acquire(&page_ref_lock);
  if (--PA2PGREF(*pa) <= 0) {
    
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&page_ref_lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    PA2PGREF(*pa) = 1; // 新分配的, 无需加速
  }
  return (void*)r;
}

void* kcopy_deref_page(void* pa) {
  acquire(&page_ref_lock);
  if (PA2PGREF(*pa) <= 1) {
    release(&page_ref_lock);
    return pa;
  }
  void *new_pa = kalloc();
  if (new_pa == 0) {
    // panic("kcopy_deref_page: kalloc");
    release(&page_ref_lock);
    return 0;
  }
  memmove(new_pa, pa, PGSIZE);
  PA2PGREF(*pa)--;
  release(&page_ref_lock);
  return new_pa;
}

void kref_page(void *pa) {
  acquire(&page_ref_lock);
  PA2PGREF(*pa)++;
  release(&page_ref_lock);
}
