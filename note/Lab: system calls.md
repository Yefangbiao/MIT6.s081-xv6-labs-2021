# Lab: system calls

## System call tracing ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

1.`sysproc.c`

```c
uint64
sys_trace(void)
{
    int mask;
    if(argint(0, &mask) < 0)
        return -1;
    struct proc *p = myproc();
    p->trace_mask |= mask;
    return 0;
}
```

从0获取传进来的参数.

2.`proc.h`的`proc`结构体添加属性

```c
// this is for sys_trace()
  int trace_mask;
```



2.`proc.c`的`fork`函数。

添加一句,每一次fork都会复制变量

```c
// copy trace mask
 np->trace_mask = p->trace_mask;
```

3.在`syscall.c`的调用函数添加

在每次系统调用的时候进行检查，然后打印.

```c
void
syscall(void)
{
    int num;
    struct proc *p = myproc();

    num = p->trapframe->a7;
    if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        uint64 ret = syscalls[num]();
        p->trapframe->a0 = ret;
        if((1 << num) & (p->trace_mask)) {
            printf("%d: syscall %s -> %d\n", p->pid, syscall_name[num], ret);
        }
    } else {
        printf("%d %s: unknown sys call %d\n",
               p->pid, p->name, num);
        p->trapframe->a0 = -1;
    }
}
```



## Sysinfo ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

1.`在 Makefile中将 $U/_sysinfotest`添加到 UPROGS

2.`要在 user/user.h 中`声明sysinfo()。预先声明`struct sysinfo`

3.记得在`sysproc.c`和`syscall.c` 添加`#include "sysinfo.h"`。在`syscall.h分配编号`

4.添加entry。



主要在三个地方添加代码

1.`kalloc.c`计算空余内存。 我们需要查看`kfree`和`kalloc`函数。发现内存放在了一个链表里，链表每个节点的内存大小是`PGSIZE`。我们统计链表节点的个数即可。

```c
// get free memory
uint64 get_free_mem(void) {
    uint64 result = 0;

    struct run *r;
    acquire(&kmem.lock);
    r = kmem.freelist;
    while(r) {
        result += PGSIZE;
        r = r -> next;
    }
    release(&kmem.lock);
    return result;
}

```

2.`proc.c`计算进程的数目，需要除去`UNUSED`。

查看分配的代码，我们可以发现所有的进程存储在`proc[NPROC]`里面。我们只要计算`proc[NPROC]`里面进程的数目即可。

```c
// get proc number
uint64 get_proc_num(void) {
    struct proc *p;

    int count = 0;

    for(p = proc; p < &proc[NPROC]; p++){
        acquire(&p->lock);
        if(p->state != UNUSED){
            count++;
        }
        release(&p->lock);
    }

    return count;
}
```

3.`sysproc.c`

sysinfo 调用进程

```c
uint64
sys_sysinfo(void)
{
    struct sysinfo info;
    info.freemem=get_free_mem();
    info.nproc=get_proc_num();

    struct proc *p = myproc();
    uint64 addr; // user pointer to struct sysinfo

    if(argaddr(0, &addr) < 0)
        return -1;

    if(copyout(p->pagetable, addr, (char *) &info, sizeof(info)) < 0)
        return -1;

    return 0;
}
```

这里需要注意的问题在于，传进来了一个参数，我们通过`0`得到。通过`argaddr`计算地址。通过`copyout`传回。