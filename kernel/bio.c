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

#define NBUCKET 13

struct {
  struct spinlock lock[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf hashtable[NBUCKET];
} bcache;

int hash(int x){
  return x % NBUCKET;
}

uint64 count = 0;

void
binit(void)
{
  struct buf *b;

  // Create hashed table of buffers
  for(int i=0; i<NBUCKET; i++){
    initlock(&bcache.lock[i], "bcache");
    bcache.hashtable[i].next = 0;
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->next = bcache.hashtable[0].next;
    bcache.hashtable[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int h = hash(blockno);

  acquire(&bcache.lock[h]);
  // Is the block already cached?
  for(b = bcache.hashtable[h].next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[h]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[h]);
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  struct buf* prevb;
  struct buf* minbuf=0; //least recently used
  struct buf* prevminbuf=0;
  uint maxtime = 0;
  int max_bucket = -1;
  // find the least recently used unused buffer
  for(int i=0; i<NBUCKET; i++){
    int flag = 0;
    acquire(&bcache.lock[i]);
    for(b=bcache.hashtable[i].next, prevb=&bcache.hashtable[i]; b!=0; b=b->next, prevb=prevb->next){
      if(b->refcnt==0 && b->timestamp >= maxtime){
        minbuf = b;
        prevminbuf = prevb;
        maxtime = b->timestamp;
        flag=1;
      }
    }
    // release lock in lne 95
    if(flag){
      if(max_bucket != -1) release(&bcache.lock[max_bucket]); //release max_bucket-before(not released)
      max_bucket = i;
    }
    else release(&bcache.lock[i]);
  }

  if(minbuf != 0){
    prevminbuf->next = minbuf->next; // pop minbuf from hashtable-list
    release(&bcache.lock[max_bucket]);
  }

  acquire(&bcache.lock[h]);
  if (minbuf != 0){
    minbuf->next = bcache.hashtable[h].next;
    bcache.hashtable[h].next = minbuf;
  }

  // A didn't find in line 76-83, but find here because B create it.
  for(b = bcache.hashtable[h].next; b!=0; b=b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[h]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  if (minbuf == 0) panic("bget: no buffers");

  minbuf->dev = dev;
  minbuf->blockno = blockno;
  minbuf->valid = 0;
  minbuf->refcnt = 1;

  release(&bcache.lock[h]);
  acquiresleep(&minbuf->lock);
  return minbuf;
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

  int h = hash(b->blockno);
  acquire(&bcache.lock[h]);

  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->timestamp = ticks;
  }
  
  release(&bcache.lock[h]);
}

void
bpin(struct buf *b) {
  int h = hash(b->blockno);
  acquire(&bcache.lock[h]);
  b->refcnt++;
  release(&bcache.lock[h]);
}

void
bunpin(struct buf *b) {
  int h = hash(b->blockno);
  acquire(&bcache.lock[h]);
  b->refcnt--;
  release(&bcache.lock[h]);
}


