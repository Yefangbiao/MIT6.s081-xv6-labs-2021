## Speed up system calls ([easy](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

这个实验的目的就是用一个新的页进行映射。

首先看看预定义的函数

```c
int
ugetpid(void)
{
  struct usyscall *u = (struct usyscall *)USYSCALL;
  return u->pid;
}
```

ugetpid()直接从USYSCALL这个地址读数据.



0.记得在`proc`结构体添加`usyscall`

1.主要的代码需要在`proc.c/proc_pagetable` 实现.

根据上面的例子添加一个系统调用。

```c
// map the USYSCALL
    if (mappages(pagetable, USYSCALL, PGSIZE, (uint64)(p->usyscall),
                 PTE_R | PTE_U) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmunmap(pagetable, TRAPFRAME, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

return pagetable;
```

注意如果申请失败需要把前面的`uvmunmap`和`uvmfree`等释放掉.



2.接着需要在分配的时候分配usyscall的page

`proc.c/allocproc`

```c
// Allocate a usyscall page.
    if ((p->usyscall = (struct usyscall *)kalloc()) == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }
    
// An empty user page table.
```

最后也需要记录pid

```c
p->usyscall->pid = p->pid;
return p;
```



3.最后需要在`proc.c/freeproc`清零释放

```c
if (p->usyscall) kfree((void *)p->usyscall); //释放页面
    p->usyscall = 0;//清零
```

并且在`proc_freepagetable`函数释放页表
```c
uvmunmap(pagetable, USYSCALL, 1, 0);
```



Note:使用`./grade-lab-pgtbl pgtbltest`测试可能会遇到问题->pgtbltest: pgaccess 失败。

这个没关系，需要完成后面的。



## Print a page table ([easy](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

在`vm.c`添加两个函数，其他根据实验要求添加

```c
void vmprint_level(pagetable_t pt, int level) {

    for(int i = 0; i < 512; i++){
        pte_t pte = pt[i];
        if(pte & PTE_V){
            if (level==2) printf("..");
            if (level==1) printf(".. ..");
            if (level==0) printf(".. .. ..");
            uint64 pa = PTE2PA(pte);
            printf("%d: pte %p pa %p\n", i, pte, pa);
            if(level != 0){
            vmprint_level((pagetable_t)pa, level - 1);
            }
        }
    }
}

void vmprint(pagetable_t pt) {
    printf("page table %p\n", pt);
    vmprint_level(pt, 2);
}
```

1.pagetable_t 本身是一个地址，根据定义

```c
typedef uint64 *pagetable_t; // 512 PTEs
```

把它想成一个数组

```c
#define PTE2PA(pte) (((pte) >> 10) << 12)
```

转换`PTE`到`下一个的页表的地址`



## Detecting which pages have been accessed ([hard](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

1.使用`argaddr`/`argint`将用户态传过来的参数存起来。

2.使用`walk`函数，得到页面。 因为用户态用的是虚拟地址，需要转换为物理地址.

3.检查是否被访问，查阅RISC-V手册。 `#define PTE_A (1L << 6)`

4.检查后需要记录到二进制的int里面。并且记得将访问位清零

```c
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
   struct proc* p = myproc();
  uint64 buf;   // 检测的页起始地址
  int pagenum;  // 页数目
  uint64 abits;    // 返回地址

  int bitmap = 0;

  if(argaddr(0, &buf) < 0)
    return -1;
  if(argint(1, &pagenum) < 0)
    return -1;
  argaddr(2, &abits);

    uint64 complement=PTE_A;
    complement=~complement;

  for (int i=0;i<pagenum;i++) {
      uint64 page = buf + i*PGSIZE;
      pte_t* pte = walk(p->pagetable,page,0);
      if ((*pte) & PTE_A) {
          bitmap=bitmap|(1<<i);
          *pte=(*pte)&complement;
      }
  }

    copyout(p->pagetable, abits, (char *)&bitmap, sizeof (bitmap));

  return 0;
}
```


