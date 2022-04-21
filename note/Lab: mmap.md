# Lab: mmap ([hard](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

参考:https://juejin.cn/post/7022394470419136542





`mmap`即内存映射文件，将一个文件直接映射到内存当中，之后对文件的读写就可以直接通过对内存进行读写来进行，而对文件的同步则由操作系统来负责完成。使用`mmap`可以避免对文件大量`read`和`write`操作带来的内核缓冲区和用户缓冲区之间的频繁的数据拷贝。



`Keep track of what `mmap` has mapped for each process. Define a structure corresponding to the VMA (virtual memory area) described in Lecture 15, recording the address, length, permissions, file, etc. for a virtual memory range created by `mmap`. Since the xv6 kernel doesn't have a memory allocator in the kernel, it's OK to declare a fixed-size array of VMAs and allocate from that array as needed. A size of 16 should be sufficient.`

根据hint，定义VMA结构体，大小为16

`proc.h`

```c
struct vma {
    int valid;
    uint64 addr;
    uint64 length;
    int prot;
    int flags;
    struct file *fd;
    uint64 offset;
};

#define VMA_SIZE 16

struct proc {
  // ...
  struct vma vmas[VMA_SIZE];
};
```



实现mmap系统调用.

`Fill in the page table lazily, in response to page faults. That is, `mmap` should not allocate physical memory or read the file. Instead, do that in page fault handling code in (or called by) `usertrap`, as in the lazy page allocation lab. The reason to be lazy is to ensure that `mmap` of a large file is fast, and that `mmap` of a file larger than physical memory is possible.`

mmap不分配，而是惰性的分配

`sysfile.c`

```c
uint64
sys_mmap(void)
{
    uint64 addr, length, offset;
    int prot, flags, fd;
    struct file *f;

    // get args
    if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 || argint(2, &prot) < 0
       || argint(3, &flags) < 0 || argfd(4, &fd, &f) < 0 || argaddr(5, &offset) < 0)
        return -1;

    // check error
    if((!f->readable && (prot & (PROT_READ)))
       || (!f->writable && (prot & PROT_WRITE) && !(flags & MAP_PRIVATE)))
        return -1;

    // 长度向上取证，分配一整页
    length = PGROUNDUP(length);

    struct proc *p = myproc();
    struct vma *v = 0;
    uint64 vaend = MMAPEND;

    // Find a free vma, and calculate where to map the file along the way.
    for(int i=0;i<VMA_SIZE;i++) {
        struct vma *vv = &p->vmas[i];
        if(vv->valid == 0) {
            if(v == 0) {
                v = &p->vmas[i];
                // found free vma;
                v->valid = 1;
            }
        } else if(vv->addr < vaend) {
            vaend = PGROUNDDOWN(vv->addr);
        }
    }

    if(v == 0){
        panic("mmap: no free vma");
    }

    v->addr = vaend - length;
    v->length = length;
    v->prot = prot;
    v->flags = flags;
    v->f = f; // assume f->type == FD_INODE
    v->offset = offset;

    filedup(v->f);

    return v->addr;
}
```

在`usertrap`分配内存

```c
else if ((r_scause() == 13 || r_scause() == 15)) {
      uint64 va = r_stval();
      if (!vmatrylazytouch(va)) {
          goto abnormal;
      }
  } else {
    abnormal:
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
```

`sysfile.c`

```c
// find a vma using a virtual address inside that vma.
struct vma *findvma(struct proc *p, uint64 va) {
    for(int i=0;i<VMA_SIZE;i++) {
        struct vma *vv = &p->vmas[i];
        if(vv->valid == 1 && va >= vv->addr && va < vv->addr + vv->length) {
            return vv;
        }
    }
    return 0;
}

// finds out whether a page is previously lazy-allocated for a vma
// and needed to be touched before use.
// if so, touch it so it's mapped to an actual physical page and contains
// content of the mapped file.
int vmatrylazytouch(uint64 va) {
    struct proc *p = myproc();
    struct vma *v = findvma(p, va);
    if(v == 0) {
        return 0;
    }

    // allocate physical page
    void *pa = kalloc();
    if(pa == 0) {
        panic("vmalazytouch: kalloc");
    }
    memset(pa, 0, PGSIZE);

    // read data from disk
    begin_op();
    ilock(v->f->ip);
    readi(v->f->ip, 0, (uint64)pa, v->offset + PGROUNDDOWN(va - v->addr), PGSIZE);
    iunlock(v->f->ip);
    end_op();

    // set appropriate perms, then map it.
    int perm = PTE_U;
    if(v->prot & PROT_READ)
        perm |= PTE_R;
    if(v->prot & PROT_WRITE)
        perm |= PTE_W;
    if(v->prot & PROT_EXEC)
        perm |= PTE_X;

    if(mappages(p->pagetable, va, PGSIZE, (uint64)pa, PTE_R | PTE_W | PTE_U) < 0) {
        panic("vmalazytouch: mappages");
    }

    return 1;
}

```



实现munmap

`Implement `munmap`: find the VMA for the address range and unmap the specified pages (hint: use `uvmunmap`). If `munmap` removes all pages of a previous `mmap`, it should decrement the reference count of the corresponding `struct file`. If an unmapped page has been modified and the file is mapped `MAP_SHARED`, write the page back to the file. Look at `filewrite` for inspiration.`

`sysfile.c`

```c
// find a vma using a virtual address inside that vma.
struct vma *findvma(struct proc *p, uint64 va) {
    for(int i=0;i<VMA_SIZE;i++) {
        struct vma *vv = &p->vmas[i];
        if(vv->valid == 1 && va >= vv->addr && va < vv->addr + vv->length) {
            return vv;
        }
    }
    return 0;
}

uint64
sys_munmap(void)
{
    uint64 addr, length;

    if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0)
        return -1;

    struct proc *p = myproc();

    struct vma *v;
    v = findvma(p, addr);
    if(v == 0) {
        return -1;
    }

    if(addr > v->addr && addr + length < v->addr + v->length) {
        // trying to "dig a hole" inside the memory range.
        return -1;
    }

    uint64 addr_aligned = addr;
    if(addr > v->addr) {
        addr_aligned = PGROUNDUP(addr);
    }

    int nunmap = length - (addr_aligned-addr); // nbytes to unmap
    if(nunmap < 0)
        nunmap = 0;

    vmaunmap(p->pagetable, addr_aligned, nunmap, v); // custom memory page unmap routine for mmapped pages.

    if(addr <= v->addr && addr + length > v->addr) { // unmap at the beginning
        v->offset += addr + length - v->addr;
        v->addr = addr + length;
    }
    v->length -= length;

    if(v->addr <= 0) {
        fileclose(v->f);
        v->valid = 0;
    }

    return 0;
}
```

将一个 vma 所分配的所有页释放，并在必要的情况下，将已经修改的页写回磁盘。



这里首先通过传入的地址找到对应的 vma 结构体（通过前面定义的 findvma 方法），然后检测了一下在 vma 区域中间“挖洞”释放的错误情况，计算出应该开始释放的内存地址以及应该释放的内存字节数量（由于页有可能不是完整释放，如果 addr 处于一个页的中间，则那个页的后半部分释放，但是前半部分不释放，此时该页整体不应该被释放）。

计算出来释放内存页的开始地址以及释放的个数后，调用自定义的 vmaunmap 方法（vm.c）对物理内存页进行释放，并在需要的时候将数据写回磁盘。将该方法独立出来并写到 vm.c 中的理由是方便调用 vm.c 中的 walk 方法。

在调用 vmaunmap 释放内存页之后，对 v->offset、v->vastart 以及 v->sz 作相应的修改，并在所有页释放完毕之后，关闭对文件的引用，并完全释放该 vma。

`vm.c`

```c
// Remove n BYTES (not pages) of vma mappings starting from va. va must be
// page-aligned. The mappings NEED NOT exist.
// Also free the physical memory and write back vma data to disk if necessary.
void
vmaunmap(pagetable_t pagetable, uint64 va, uint64 nbytes, struct vma *v)
{
    uint64 a;
    pte_t *pte;

    // borrowed from "uvmunmap"
    for(a = va; a < va + nbytes; a += PGSIZE){
        if((pte = walk(pagetable, a, 0)) == 0)
            panic("sys_munmap: walk");
        if(PTE_FLAGS(*pte) == PTE_V)
            panic("sys_munmap: not a leaf");
        if(*pte & PTE_V){
            uint64 pa = PTE2PA(*pte);
            if((*pte & PTE_D) && (v->flags & MAP_SHARED)) { // dirty, need to write back to disk
                begin_op();
                ilock(v->f->ip);
                uint64 aoff = a - v->addr; // offset relative to the start of memory range
                if(aoff < 0) { // if the first page is not a full 4k page
                    writei(v->f->ip, 0, pa + (-aoff), v->offset, PGSIZE + aoff);
                } else if(aoff + PGSIZE > v->length){  // if the last page is not a full 4k page
                    writei(v->f->ip, 0, pa, v->offset + aoff, v->length - aoff);
                } else { // full 4k pages
                    writei(v->f->ip, 0, pa, v->offset + aoff, PGSIZE);
                }
                iunlock(v->f->ip);
                end_op();
            }
            kfree((void*)pa);
            *pte = 0;
        }
    }
}
```



最后需要做的，是在 proc.c 中添加处理进程 vma 的各部分代码。

- 让 allocproc 初始化进程的时候，将 vma 槽都清空
- freeproc 释放进程时，调用 vmaunmap 将所有 vma 的内存都释放，并在需要的时候写回磁盘
- fork 时，拷贝父进程的所有 vma，但是不拷贝物理页



```c
// kernel/proc.c

static struct proc*
allocproc(void)
{
  // ......

  // Clear VMAs
  for(int i=0;i<NVMA;i++) {
    p->vmas[i].valid = 0;
  }

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  for(int i = 0; i < NVMA; i++) {
    struct vma *v = &p->vmas[i];
    vmaunmap(p->pagetable, v->vastart, v->sz, v);
  }
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  // ......

  // copy vmas created by mmap.
  // actual memory page as well as pte will not be copied over.
  for(i = 0; i < NVMA; i++) {
    struct vma *v = &p->vmas[i];
    if(v->valid) {
      np->vmas[i] = *v;
      filedup(v->f);
    }
  }

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  np->state = RUNNABLE;

  release(&np->lock);

  return pid;
}

```


