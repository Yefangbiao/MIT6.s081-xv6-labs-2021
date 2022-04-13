# Lab: Xv6 and Unix utilities

[代码地址](https://github.com/Yefangbiao/MIT6.s081-xv6-labs-2021/tree/util)



## sleep ([easy](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

这个实验相对还是比较简单的，根据提示和样例代码，我们可以很容易的写出代码。

这里遇到了一个坑：没有看完提示，忽略了这句话

`Add your `sleep` program to `UPROGS` in Makefile; once you've done that, `make qemu` will compile your program and you'll be able to run it from the xv6 shell.`

我们在`Makefile`添加如下代码

```makefile
UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_sleep\    --- <--  添加了这一行
```



## pingpong ([easy](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

也是相对简单的一道题目，同样不要忘了在Makefile添加代码。

这里我遇到的问题是

```
child: print "<pid>: received ping"
parent: print "<pid>: received pong"
```

需要注意的是，在代码里需要加上换行符。。。 纠结了我好久

```c
printf("%d: received ping\n", getpid());
printf("%d: received pong\n", getpid());
```



## primes ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))/([hard](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

有点难的一道题目。[官方参考](https://swtch.com/~rsc/thread/)

需要不断递归的创建`fork`子进程并且使用`pipe`传递。

需要注意的是关闭`读管道`和`写管道`的时机。



需要注意的是。子进程和父进程需要同时关闭管道描述符，因为子进程copy了一份。



## find ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

参考`ls.c`递归调用，不同之处在于

1.递归到`.`或者`..`需要跳过

```c
if (de.name[0] == '.' && de.name[1] == 0) continue;
if (de.name[0] == '.' && de.name[1] == '.' && de.name[2] == 0) continue;
```



2.递归到目录需要一直递归下去



## xargs ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

1.传递的参数需要通过0得到，一次取一行

```c
while(read(0, p, 1) != 0){
    if(*p == '\n' || *p == '\0'){
        *p = '\0';
        return buf;
    }
    p++;
}
```