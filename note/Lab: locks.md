# Lab: locks

## Memory allocator ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

从全局的freelist，切换为每个cpu维护一个自己的freelist

首先修改结构体和初始化.`kalloc.c`

```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for(int i=0;i<NCPU;i++) {
      initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}
```



根据提示，每次获取`cpuid`的时候需要使用`push_off()`和`pop_off()`

`kfree.c`

```c
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
  int index = cpuid();

  acquire(&kmem[index].lock);
  r->next = kmem[index].freelist;
  kmem[index].freelist = r;
  release(&kmem[index].lock);

  pop_off();
}
```

`kalloc.c`分配的时候如果没有了，需要从其他cpu偷.我这里只偷了一个,有些影响性能。

```c
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int index = cpuid();

  acquire(&kmem[index].lock);
  if (!kmem[index].freelist) {
      // no freelist
      for (int i=0;i<NCPU;i++){
          if (i==index) continue;
          acquire(&kmem[i].lock);
          struct run *other_r;
          other_r = kmem[i].freelist;
          if (other_r) {
              kmem[i].freelist = other_r->next;
              other_r->next=0;
              kmem[index].freelist = other_r;
              release(&kmem[i].lock);
              break;
          }
          release(&kmem[i].lock);
      }
  }

  r = kmem[index].freelist;
  if(r)
    kmem[index].freelist = r->next;
  release(&kmem[index].lock);

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
```



## Buffer cache ([hard](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

原先的缓冲区是全局的，对于申请或者释放需要全局加锁。

通过这个实验将缓冲区改为一个个桶，对桶进行加锁解锁.

`buf.h`。

根据提示添加一个`timestamp`字段，记录最后的使用时间.

再添加一个hashno。记录是哪个`bucket`。

```c
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
//  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];

  uint timestamp;
  int hashno;
};
```



`It is OK to use a fixed number of buckets and not resize the hash table dynamically. Use a prime number of buckets (e.g., 13) to reduce the likelihood of hashing conflicts.`

根据提示:定义哈希表结构，使用固定数量的桶。

```c
#define NBUCKET 13

struct bucket {
    struct spinlock lock;
    struct buf head;
} hashtable[NBUCKET];

int gethash(uint blockno) {
    return blockno % NBUCKET;
}
```



`binit`。

分配给每个桶buffer。记住，每个桶可能不一样大小。

```c
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
      initsleeplock(&b->lock, "buffer");
  }

  b = bcache.buf;
  for (int i=0;i<NBUCKET;i++) {
      initlock(&hashtable[i].lock, "bucket");
      for (int j=0; j<NBUF/NBUCKET && b; j++) {
          b->hashno = i; // hash(b) should equal to i
          b->next = hashtable[i].head.next;
          hashtable[i].head.next = b;
          b++;
      }
  }
}
```



`brelse`

释放锁的时候同时维护`timestamp`

```c
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket_index = gethash(b->blockno);
  acquire(&hashtable[bucket_index].lock);

  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->timestamp = ticks;
  }

  release(&hashtable[bucket_index].lock);
}
```

`bget`

这里主要分为几个情况，1.已经在bucket中。2.没有在bucket中但是bucket有空闲的。3.bucket满了但是其他的有

```c
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int bucket_index = gethash(blockno);
  acquire(&hashtable[bucket_index].lock);

  struct bucket* bucket = &hashtable[bucket_index];

  // Is the block already cached?
  for(b = bucket->head.next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno && b->hashno == bucket_index){
      b->refcnt++;
      b->hashno=bucket_index;
      release(&hashtable[bucket_index].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // find an empty buf in this bucket
  int min_time = 0x8fffffff;
  struct buf* replace_buf = 0;

  for(b = bucket->head.next; b != 0; b = b->next){
      if(b->refcnt == 0 && b->timestamp < min_time) {
          replace_buf = b;
          min_time = b->timestamp;
      }
  }
  if (replace_buf) {
      replace_buf->dev = dev;
      replace_buf->blockno = blockno;
      replace_buf->valid = 0;
      replace_buf->refcnt = 1;
      replace_buf->hashno = bucket_index;
      release(&hashtable[bucket_index].lock);
      acquiresleep(&replace_buf->lock);
      return replace_buf;
  }

  // find an empty buf in another bucket
  // 这里需要加一个全局锁
  acquire(&bcache.lock);

  min_time = 0x8fffffff;
  loop:
  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
      if(b->refcnt == 0 && b->timestamp < min_time) {
          replace_buf = b;
          min_time = b->timestamp;
      }
  }

  if (replace_buf) {
      int another_index = replace_buf->hashno;
      acquire(&hashtable[another_index].lock);
      if(replace_buf->refcnt != 0)  // be used in another bucket's local find between finded and acquire
      {
          release(&hashtable[another_index].lock);
          goto loop;
      }
      struct buf *pre = &hashtable[another_index].head;
      struct buf *p = hashtable[another_index].head.next;
      while (p != replace_buf) {
          pre = pre->next;
          p = p->next;
      }
      printf("hi");
      // 释放 another_index's bucket
      pre->next = p->next;
      printf("yes, find one\n");
      release(&hashtable[another_index].lock);

      // 2. 加到这个bucket
      replace_buf->next = (&hashtable[bucket_index])->head.next;
      hashtable[bucket_index].head.next = replace_buf;

      release(&bcache.lock);

      // 设置属性，然后返回.
      replace_buf->dev = dev;
      replace_buf->blockno = blockno;
      replace_buf->valid = 0;
      replace_buf->refcnt = 1;
      replace_buf->hashno = bucket_index;
      release(&hashtable[bucket_index].lock);
      acquiresleep(&replace_buf->lock);
      return replace_buf;
    } else {
      panic("bget: no buffers");
    }

}
```







修改`bpin`和`bunpin`

```c
void
bpin(struct buf *b) {
  int bucket_index = gethash(b->blockno);
  acquire(&hashtable[bucket_index].lock);
  b->refcnt++;
  release(&hashtable[bucket_index].lock);
}

void
bunpin(struct buf *b) {
  int bucket_index = gethash(b->blockno);
  acquire(&hashtable[bucket_index].lock);
  b->refcnt--;
  release(&hashtable[bucket_index].lock);
}
```


