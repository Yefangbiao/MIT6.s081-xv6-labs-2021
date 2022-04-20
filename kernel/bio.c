// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
//  struct buf head;
} bcache;

#define NBUCKET 13

struct bucket {
    struct spinlock lock;
    struct buf head;
} hashtable[NBUCKET];

int gethash(uint blockno) {
    return blockno % NBUCKET;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
      initsleeplock(&b->lock, "buffer");
  }

  // NBUF是30，NBUCKET是13.
  // 前12个分2个。
  // 最后一个分6个
  b = bcache.buf;
  int count=0;
  for (int i=0;i<NBUCKET - 1;i++) {
      initlock(&hashtable[i].lock, "bucket");
      for (int j=0; j<NBUF/NBUCKET && b; j++) {
          b->hashno = i; // hash(b) should equal to i
          b->timestamp=0;
          b->next = hashtable[i].head.next;
          hashtable[i].head.next = b;
          b++;
          count++;
      }
  }
  // 最后一个bucket分6个
  initlock(&hashtable[NBUCKET-1].lock, "bucket");
  for (int j=0; j<6; j++) {
      b->hashno = NBUCKET-1; // hash(b) should equal to i
      b->timestamp=0;
      b->next = hashtable[NBUCKET-1].head.next;
      hashtable[NBUCKET-1].head.next = b;
      b++;
      count++;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
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

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
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


