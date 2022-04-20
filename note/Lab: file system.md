# Lab: file system

## Large files ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))



`In this assignment you'll increase the maximum size of an xv6 file. Currently xv6 files are limited to 268 blocks, or 268*BSIZE bytes (BSIZE is 1024 in xv6). This limit comes from the fact that an xv6 inode contains 12 "direct" block numbers and one "singly-indirect" block number, which refers to a block that holds up to 256 more block numbers, for a total of 12+256=268 blocks.`



`You'll change the xv6 file system code to support a "doubly-indirect" block in each inode, containing 256 addresses of singly-indirect blocks, each of which can contain up to 256 addresses of data blocks. The result will be that a file will be able to consist of up to 65803 blocks, or 256*256+256+11 blocks (11 instead of 12, because we will sacrifice one of the direct block numbers for the double-indirect block).`



查看上面两句话，其实就是要求我们允许支持二级页表。 `256*256+256+11`表示我们需要有11个`direct`。1个`singly-indirect`。1个`double-indirect`

首先修改一些定义

`fs.h`

```c
#define NDIRECT 11  // 直接块减少到11个
... 
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT * NINDIRECT)	// 最大文件数量，其实就是11 + 256 + 256*256
  
// On-disk inode structure
struct dinode {
...
  uint addrs[NDIRECT+2];   // Data block addresses
  // 数据块地址数量. 11(direct) + 1(singly-indirect) + 1(double-indirect)
};
```



`file.h`

同时修改`inode`

```c
struct inode {
// ...
  uint addrs[NDIRECT+2];
};
```



`bmap`

```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  bn -= NINDIRECT;

  if (bn < NINDIRECT * NINDIRECT) {
      // Load double-indirect block, allocating if necessary.

      // first
      if((addr = ip->addrs[NDIRECT+1]) == 0) {
          ip->addrs[NDIRECT + 1] = addr = balloc(ip->dev);
      }
      bp = bread(ip->dev, addr);
      a = (uint*)bp->data;
      if((addr = a[bn/NINDIRECT]) == 0){
          a[bn/NINDIRECT] = addr = balloc(ip->dev);
          log_write(bp);
      }
      brelse(bp);

      // second
      bn %= NINDIRECT;
      bp = bread(ip->dev, addr);
      a = (uint*)bp->data;
      if((addr = a[bn]) == 0){
          a[bn] = addr = balloc(ip->dev);
          log_write(bp);
      }
      brelse(bp);
      return addr;
  }

  panic("bmap: out of range");
}
```

`itrunc`

```c
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // release double-indirect block
  if(ip->addrs[NDIRECT+1]){
      bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
      a = (uint*)bp->data;
      for(j = 0; j < NINDIRECT; j++){
          if(a[j]) {
              // second
              struct buf *second = bread(ip->dev, a[j]);
              uint *second_a = (uint *) second->data;
              for (int k = 0; k < NINDIRECT; k++) {
                  if (second_a[k]) {
                      bfree(ip->dev, second_a[k]);
                  }
              }
              brelse(second);
              bfree(ip->dev, a[j]);
          }
      }
      brelse(bp);
      bfree(ip->dev, ip->addrs[NDIRECT+1]);
      ip->addrs[NDIRECT+1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```



## Symbolic links ([moderate](https://pdos.csail.mit.edu/6.828/2021/labs/guidance.html))

在`stat.h`添加一个定义

```c
#define T_SYMLINK  4   // symbolic link.
```



在`kernel/fcntl.h`添加一个新的定义，不能与现有的其他定义重合

```c
#define O_NOFOLLOW 0x800
```



增加`sys_symlink`。其实就相当于系统调用

`sysfile.c`

```c
uint64
sys_symlink(void)
{
    char target[MAXPATH];
    char src[MAXPATH];

    if(argstr(0, target, MAXPATH) < 0 || argstr(1, src, MAXPATH) < 0){
        return -1;
    }

    struct inode *ip;

    begin_op();
    if((ip = create(src, T_SYMLINK, 0, 0)) == 0){
        end_op();
        return -1;
    }

    if(writei(ip, 0, (uint64)target, 0, MAXPATH) != MAXPATH){
        return -1;
    }

    iunlockput(ip);
    end_op();
    return 0;
}
```



修改`open`函数。

`sysfile.c`

```c
if(ip->type == T_SYMLINK){
    if(!(omode & O_NOFOLLOW)){
        int cycle = 0;
        char target[MAXPATH];
        while(ip->type == T_SYMLINK){
            if(cycle == 10){
                iunlockput(ip);
                end_op();
                return -1; // max cycle
            }
            cycle++;
            memset(target, 0, sizeof(target));
            readi(ip, 0, (uint64)target, 0, MAXPATH);
            iunlockput(ip);
            if((ip = namei(target)) == 0){
                end_op();
                return -1; // target not exist
            }
            ilock(ip);
        }
    }
}
```