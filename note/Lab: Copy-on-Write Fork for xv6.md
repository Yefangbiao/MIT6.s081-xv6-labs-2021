# Lab: Copy-on-Write Fork for xv6

做之前可以先阅读一下:https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081/lec08-page-faults-frans/8.4-copy-on-write-fork

## Implement copy-on write([hard](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))



按照提示

1.Modify uvmcopy() to map the parent's physical pages into the child, instead of allocating new pages. Clear `PTE_W` in the PTEs of both child and parent.

`vm.c`

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
//  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);

    // 将原来的页面设置为不可写，并且打上cow标记
      *pte = ((*pte) & (~PTE_W)) | PTE_COW;

      flags = PTE_FLAGS(*pte);

    // 注释掉，不重新分配内存
//    if((mem = kalloc()) == 0)
//      goto err;
//    memmove(mem, (char*)pa, PGSIZE);

    // 这里mem改成pa，映射到相同的物理地址
    if(mappages(new, i, PGSIZE, (uint64)pa, flags) != 0){
//      kfree(mem);
      goto err;
    }

    // 对于引用计数
      refcnt_incr(pa,1);
;  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

这里需要将父进程和子进程设置为不可写，并且设置`COW`标记。查询可以发现

说明。我们需要注释掉分配空间的`kalloc`函数。

在映射的时候新的地址和老的地址 虚拟内存和物理内存映射到同一个位置。

在下面`refcnt_incr`进行引用计数.

---

`riscv.h`

```c
#define PTE_COW (1L << 8) // 1 -> cow
```

---

我们需要对引用进行计数。

```c
// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)
```

内核申请的空间从`KERNBASE`一直到`PHYSTOP`。

这部分逻辑参考了https://www.cnblogs.com/weijunji/p/xv6-study-9.html

```c
struct {
    struct spinlock lock;
    uint counter[(PHYSTOP - KERNBASE) / PGSIZE + 1];
} refcnt;

inline
        uint64
pgindex(uint64 pa){
return (pa - KERNBASE) / PGSIZE;
}

inline
void
acquire_refcnt(){
    acquire(&refcnt.lock);
}

inline
void
release_refcnt(){
    release(&refcnt.lock);
}

void
refcnt_setter(uint64 pa, int n){
    refcnt.counter[pgindex((uint64)pa)] = n;
}

inline
        uint
refcnt_getter(uint64 pa){
return refcnt.counter[pgindex(pa)];
}

void
refcnt_incr(uint64 pa, int n){
    acquire(&refcnt.lock);
    refcnt.counter[pgindex(pa)] += n;
    release(&refcnt.lock);
}
```





2.修改分配内存和回收内存时候的计数

`kfree`.只有计数为1的时候可以回收

```c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");


  acquire_refcnt();
  uint64 index = pgindex((uint64)pa);
  if (refcnt.counter[index] > 1) {
      refcnt.counter[index] -= 1;
      release_refcnt();
      return;
  }


    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    // 释放的时候充值计数器为0
    refcnt.counter[index] = 0;
    release_refcnt();

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
```

`kfree`

```c
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  if(r)
    refcnt_incr((uint64)r, 1); // set refcnt to 1
  return (void*)r;
}
```



3.修改`trap.c`的`usertrap`

```c
else if(r_scause() == 15){
      // page write fault
      uint64 va = r_stval();
      if(cowcopy(va) == -1){
          p->killed = 1;
      }
  } else if((which_dev = devintr()) != 0){
    // ok
```

4.增加`cowcopy`

```c
int
cowcopy(uint64 va){
    va = PGROUNDDOWN(va);
    pagetable_t p = myproc()->pagetable;
    pte_t* pte = walk(p, va, 0);
    uint64 pa = PTE2PA(*pte);
    uint flags = PTE_FLAGS(*pte);

    if(!(flags & PTE_COW)){
        printf("not cow\n");
        return -2; // not cow page
    }

    acquire_refcnt();
    uint ref = refcnt_getter(pa);
    if(ref > 1){
        // ref > 1, alloc a new page
        char* mem = kalloc_nolock();
        if(mem == 0)
            goto bad;
        memmove(mem, (char*)pa, PGSIZE);
        if(mappages(p, va, PGSIZE, (uint64)mem, (flags & (~PTE_COW)) | PTE_W) != 0){
            kfree(mem);
            goto bad;
        }
        refcnt_setter(pa, ref - 1);
    }else{
        // ref = 1, use this page directly
        *pte = ((*pte) & (~PTE_COW)) | PTE_W;
    }
    release_refcnt();
    return 0;

    bad:
    release_refcnt();
    return -1;
}
```

如果只有一个引用计数，直接写即可。

否则新建一个页面用于读写