# Lab: traps



## Backtrace ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

[lecture notes](https://pdos.csail.mit.edu/6.828/2021/lec/l-riscv-slides.pdf)中提示了我们stack的结构

在hint中，将当前的栈地址放到了寄存器`s0`里面，直接调用即可。

```c
static inline uint64
r_fp()
{
  uint64 x;
  asm volatile("mv %0, s0" : "=r" (x) );
  return x;
}
```



我们需要打印当前程序的返回地址。然后递归的找上一个栈。

```c
void
backtrace(void)
{
    struct proc *p=myproc();
    // frame point
    uint64 framep = r_fp();
    printf("backtrace:\n");

    while (1) {
        uint64 ret = framep-8;
        uint64 pre = framep-16;

        if (PGROUNDDOWN(framep) != p->kstack) {
            break;
        }

        printf("%p\n", *((uint64*)ret));

        framep = *((uint64*)pre);
    }
}
```

`PGROUNDDOWN`计算栈底。根据提示`Xv6 allocates one page for each stack in the xv6 kernel at PAGE-aligned address.`.一旦栈底不是分配的堆栈地址，则退出。



## Alarm ([hard](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

看看`alarm`是什么

http://c.biancheng.net/cpp/html/334.html



1.调用`sigalarm`会在指定的周期的时候，调用函数。使用`sigreturn`返回原来的程序执行位置。

我们需要保存信息。`proc.h`

```c
  // alarm
  struct trapframe *alarm_pt;       // 保存trapframe数据
  int tick_time;              // 过去了几个时钟周期
  int alarm_time;             // 几个周期需要初始化
  uint64 alarm_handler;       // 需要处理的函数
  int flag;
```

`alarm_pt`保存的要中断执行的时候的用户信息。



2.`proc.c/allocproc`

初始化信息

```c
    // Allocate a alarm_fram page.
    if((p->alarm_pt = (struct trapframe *)kalloc()) == 0){
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    p->tick_time=0;
    p->alarm_time=-1;
    p->alarm_handler=0;
    p->flag=0;
```

`freeproc`:不要忘记释放

```c
  if (p->alarm_pt)
      kfree((void*)p->alarm_pt);
  p->alarm_pt=0;

    p->tick_time=0;
    p->alarm_time=-1;
    p->alarm_handler=0;
    p->flag=0;
```



3.系统调用的协同

```c
 // ok
    if (which_dev == 2 && p->alarm_time!=-1) {
        // alarm
        p->tick_time+=1;
        if (p->alarm_time==p->tick_time) {
            if(p->flag==0)
            {
                memmove(p->alarm_pt,p->trapframe,PGSIZE);
                p->trapframe->epc=p->alarm_handler;
                p->flag=1;//只有sigreturn可以将它置为0
            }

            p->tick_time=0;
        }
    }
```

注意`memmove(p->alarm_pt,p->trapframe,PGSIZE);`执行的时候需要保存当前程序运行的程序上下文。



4.系统调用`sysproc.c`

```c
uint64
sys_sigalarm(void)
{
    struct proc* p=myproc();
    int tick;
    uint64 handler;

    argint(0, &tick);
    argaddr(1, &handler);

    p->alarm_time=tick;
    p->alarm_handler=handler;
    return 0;
}


uint64
sys_sigreturn(void)
{
    struct proc* p=myproc();
    p->flag=0;//记得p->flag重置为0
    memmove(p->trapframe,p->alarm_pt,PGSIZE);
    return 0;
}
```

`memmove(p->trapframe,p->alarm_pt,PGSIZE);`恢复程序的上下文