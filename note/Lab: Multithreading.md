# Lab: Multithreading

## Uthread: switching between threads ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

我们需要填三个地方。

`thread_create()` 、 `thread_schedule()` in `user/uthread.c`、 `thread_switch` in `user/uthread_switch.S`.

我们知道线程切换需要保存上下文信息，包括寄存器的

复制`proc.h`的`context`到`uthread.c`目录下。



在thread加上一行。

```c
struct thread {
  char       stack[STACK_SIZE]; /* the thread's stack */
  int        state;             /* FREE, RUNNING, RUNNABLE */
  struct     context context;
};
```



在`thread_create`创建的时候加上`caller`需要保存的信息

```c
void 
thread_create(void (*func)())
{
  struct thread *t;

  for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
    if (t->state == FREE) break;
  }
  t->state = RUNNABLE;
  // YOUR CODE HERE
    (t->context).sp=(uint64)t->stack+STACK_SIZE;
    (t->context).ra=(uint64)func;
}
```

`ra`用来保存线程的返回地址

`sp`是栈底。



复制`kernel\swtch.S`到`user\uthread_switch.S`中。

在`thread_schedule`线程调度的时候添加

```c
		/* YOUR CODE HERE
     * Invoke thread_switch to switch from t to next_thread:
     * thread_switch(??, ??);
     */
    thread_switch((uint64)&t->context, (uint64)&current_thread->context);
```



## Using threads ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

1.先定义一个全局锁，在`main`函数初始化

```c
pthread_mutex_t lock[NBUCKET];

int main() {
  for (int i=0;i<NBUCKET;i++) {
        pthread_mutex_init(&lock[i], NULL);
   }
}
```



在`get`和`put`函数加锁

```c
static 
void put(int key, int value)
{
  int i = key % NBUCKET;

    pthread_mutex_lock(&lock[i]);
  // is the key already present?
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
    pthread_mutex_unlock(&lock[i]);
}
```

```c
static struct entry*
get(int key)
{
  int i = key % NBUCKET;

    pthread_mutex_lock(&lock[i]);
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key) break;
  }
    pthread_mutex_unlock(&lock[i]);

  return e;
}
```



## Barrier([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

```c
pthread_cond_wait(&cond, &mutex);  // go to sleep on cond, releasing lock mutex, acquiring upon wake up
pthread_cond_broadcast(&cond);     // wake up every thread sleeping on cond
```

```c
static void 
barrier()
{
  // YOUR CODE HERE
  //
  // Block until all threads have called barrier() and
  // then increment bstate.round.
  //
    pthread_mutex_lock(&lock);
    bstate.nthread += 1;
    if (bstate.nthread == nthread) {
        bstate.round++;
        bstate.nthread=0;
        pthread_cond_broadcast(&bstate.barrier_cond);
    } else {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&lock);
}
```




