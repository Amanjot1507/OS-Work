/* Author: Adem Efe Gencer, November 2015.
 *
 * This code implements a set of virtualized block store on top of another block store.
 * Each virtualized block store is identified by a so-called "inode number", which indexes
 * into an array of inodes.. The interface is as follows:
 *
 *     void ufsdisk_create(block_if below, unsigned int n_inodes, unsigned int magic_number)
 *         Initializes the underlying block store "below" with a file system.
 *         The file system consists of one "superblock", a number inodes, and
 *         the remaining blocks explained below.
 *
 *     block_if ufsdisk_init(block_if below, unsigned int inode_no)
 *         Opens a virtual block store at the given inode number.
 *
 * The layout of the file system is described in the file "ufsdisk.h".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "block_if.h"
#include "ufsdisk.h"

static block_t null_block;			// a block filled with null bytes

/* The state of a UFS virtual block store, which is identified by an inode number.
 */
struct ufs_state {
  block_if below;			// block store below
  unsigned int inode_no;	// inode number in file system
};

/* Snapshot of the file system
 */
struct ufs_snapshot {
  union ufs_block superblock;
  union ufs_block inodeblock;
  block_no inode_blockno;
  struct ufs_inode *inode;
};

//Null Block
static block_t null_block;
static int write_next_free_block(block_if, block_t *, block_no *free_block_no);
static int set_block_free(block_if, block_no);
/*
 * Get the snapshot of the filesystem
 */
static int ufsdisk_get_snapshot(struct ufs_snapshot *snapshot, block_if below, unsigned int inode_no){
  if(below == NULL){
    fprintf(stderr,"!!UFSERR: underlying file system not initialized\n");
    return -1;
  }

  if(inode_no < 0)
    {
      fprintf(stderr, "!!UFSERR: Invalid inode number (less than 0)\n");
    }
  //Get the superblock
  if ((*below->read)(below, 0, (block_t *) &snapshot->superblock) < 0) {
    return -1;
  }

  // Check the inode number
  if (inode_no >= snapshot->superblock.superblock.n_inodeblocks * INODES_PER_BLOCK) {
    fprintf(stderr, "!!UFSERR: inode number too large %u %u\n", inode_no, snapshot->superblock.superblock.n_inodeblocks);
    return -1;
  }

  // Find the inode
  snapshot->inode_blockno = 1 + inode_no / INODES_PER_BLOCK;
  if ((*below->read)(below, snapshot->inode_blockno, (block_t *) &snapshot->inodeblock) < 0) {
    return -1;
  }
  snapshot->inode = &snapshot->inodeblock.inodeblock.inodes[inode_no % INODES_PER_BLOCK];
  return 0;

}

/* Retrieve the number of blocks in the file referenced by 'bi'. This information
 * is maintained in the inode itself.
 */
static int ufsdisk_nblocks(block_if bi){
  if(bi == NULL){
    fprintf(stderr,"!!UFSERR: File not initialized at this inode\n");
    return -1;
  }

  struct ufs_state *us = (struct ufs_state *) bi->state;

  struct ufs_snapshot snapshot;

  if(ufsdisk_get_snapshot(&snapshot, us->below, us->inode_no) < 0){
    return -1;
  }

  return snapshot.inode->nblocks;
}

static int recurse_helper(block_if bi, block_no b, int nlevels, int *n_blocks){
  block_t read_block;
  struct ufs_state *us = bi->state;

  if(*n_blocks == 0){
    return 0;
  }

  //Nlevels = 0, reached a data block - set it to zero
  if(nlevels == 0){
    if((*us->below->write)(us->below, b, &null_block) < 0){
      return -1;
    }
    set_block_free(bi, b);

    (*n_blocks)--;
    return 0;
  }

  //Indirect block, read it
  if((*us->below->read)(us->below, b, &read_block) < 0){
    return -1;
  }

  //Call the function recursively on every reference in the indirect block
  int j = 0;
  struct ufs_indirblock *indir = (struct ufs_indirblock *) &read_block;

  while(j < REFS_PER_BLOCK){
    if(indir->refs[j] != 0)
      recurse_helper(bi, indir->refs[j], nlevels - 1, n_blocks);
    else
      (*n_blocks)--;
    j++;
  }
	
  //Set the block to 0
  if((*us->below->write)(us->below, b, &null_block) < 0){
    return -1;
  }
  else {
    set_block_free(bi, b);
  }
  return 0;
}

/* Set the size of the file 'bi' to 'nblocks'.
 */
static int ufsdisk_setsize(block_if bi, block_no nblocks){

  if (bi == NULL) {
    fprintf(stderr,"!!UFSERR: File not initialized at this inode\n");
    return -1;
  }

  if (nblocks < 0) {
    fprintf(stderr, "UFSERR: Invalid file size (less than 0)\n");
    return -1;
  }

  struct ufs_state *us = (struct ufs_state *) bi->state;

  struct ufs_snapshot snapshot;

  if (ufsdisk_get_snapshot(&snapshot, us->below, us->inode_no) < 0) {
    return -1;
  }

  if (nblocks == snapshot.inode->nblocks) {
    return nblocks;
  }

  //TODO : Set size to nblocks. Allocate or deallocate blocks of the inode,
  //		 set size and manipulate free list accordingly

  int n_blocks = snapshot.inode->nblocks;
  int index = 0;

  for(index=0;index<REFS_PER_INODE;index++){
    
    if(index < REFS_PER_INODE - 3){
    	if(snapshot.inode->refs[index] == 0){
    		n_blocks--;
    		continue;
    	}
      recurse_helper(bi, snapshot.inode->refs[index], 0, &n_blocks);
    }
    else
      if(index == REFS_PER_INODE - 3){
      	if(snapshot.inode->refs[index] == 0){
    		n_blocks -= REFS_PER_BLOCK;
    		continue;
    	}
	recurse_helper(bi, snapshot.inode->refs[index], 1, &n_blocks);
      }
      else
	if(index == REFS_PER_INODE - 2){
		if(snapshot.inode->refs[index] == 0){
    		n_blocks -= REFS_PER_BLOCK*REFS_PER_BLOCK;
    		continue;
    	}
	  recurse_helper(bi, snapshot.inode->refs[index], 2 , &n_blocks);
	}
	else{
		if(snapshot.inode->refs[index] == 0){
    		nblocks--;
    		continue;
    	}
	  recurse_helper(bi, snapshot.inode->refs[index], 3 , &n_blocks);
	}
  }
  snapshot.inode->nblocks = 0;
  (*us->below->write)(us->below, snapshot.inode_blockno, (block_t *) &snapshot.inodeblock.inodeblock);

  return 0;
}

/* Read a block at the given block number 'offset' and return in *block.
 */
static int ufsdisk_read(block_if bi, block_no offset, block_t *block){
  if(bi == NULL){
    fprintf(stderr,"!!UFSERR: File not initialized at this inode\n");
    return -1;
  }

  if(offset < 0){
      fprintf(stderr, "!!UFSERR: Invalid read offset (less than 0)\n");
  }

  struct ufs_state *us = bi->state;
  struct ufs_snapshot snapshot;

  if(ufsdisk_get_snapshot(&snapshot, us->below, us->inode_no) < 0){
    return -1;
  }

  if(offset > snapshot.inode->nblocks){
    fprintf(stderr, "!!UFSERR: Invalid read offset (greater than file size)\n");
    return -1;
  }

  //Number of levels to reach the data block and the root block number
  int nlevels = 0;
  block_no b = 0;

  //Block can be reached by a direct block pointer
  if(offset < REFS_PER_INODE - 3){
    b = snapshot.inode->refs[offset];
    nlevels = 0;
  }
  //Block can be reached by a single indirect pointer
  else if(offset - (REFS_PER_INODE - 3) < REFS_PER_BLOCK){
    b = snapshot.inode->refs[REFS_PER_INODE - 3];
    offset -= REFS_PER_INODE - 3;
    nlevels = 1;
  }
  //Block can be reached by a double indirect pointer
  else if(offset - (REFS_PER_INODE - 3) - REFS_PER_BLOCK < REFS_PER_BLOCK * REFS_PER_BLOCK){
    b = snapshot.inode->refs[REFS_PER_INODE - 2];
    offset -= REFS_PER_INODE - 3 + REFS_PER_BLOCK;
    nlevels = 2;
  }
  //Block can be reached by the triple indirect pointer
  else{
    b = snapshot.inode->refs[REFS_PER_INODE - 1];
    offset -= REFS_PER_INODE - 3 + REFS_PER_BLOCK*REFS_PER_BLOCK;
    nlevels = 3;
  }
  unsigned int index = offset;
  //Traverse the tree
  for( ; ; ){

    //Hole, Return an empty block
    if(b == 0){
      memset(block, 0, BLOCK_SIZE);
      return 0;
    }

    //If read fails
    if((*us->below->read)(us->below, b, block) < 0){
      memset(block, 0 , BLOCK_SIZE);
      return -1;
    }
    //Data Block has been reached
    if(nlevels == 0){
      return 0;
    }

    //The block read is an indirect block. Index into this block appropriately and read again
    nlevels--;
    //int index = offset, 
    int j = nlevels;
    int divider = 1;
    // Divide the offset by REFS_PER_BLOCK ^ nlevels
    for (j = nlevels; j > 0; j--) {
    	divider *= REFS_PER_BLOCK;
    }
    
    //index %= REFS_PER_BLOCK;
    struct ufs_indirblock *indir = (struct ufs_indirblock *) block;
    b = indir->refs[index / divider];
    index %= divider;  
   }

  return 0;
}

/* Write *block at the given block number 'offset'.
 */
static int ufsdisk_write(block_if bi, block_no offset, block_t *block)
{
  struct ufs_state *us = bi->state;
  struct ufs_snapshot snapshot;

  if (ufsdisk_get_snapshot(&snapshot, us->below, us->inode_no) < 0){
    return -1;
  }

  block_no index = offset;
  block_no b;
  int nlevels;
  block_no ref_block = 0;
  if (index < REFS_PER_INODE - 3) {
    b = snapshot.inode->refs[index];
    nlevels = 0;
  }

  // Block can be reached by a single indirect pointer
  else if (index - (REFS_PER_INODE - 3) < REFS_PER_BLOCK) {
    b = snapshot.inode->refs[REFS_PER_INODE - 3];
    ref_block = REFS_PER_INODE - 3;
    index -= REFS_PER_INODE - 3;
    nlevels = 1;
  }

  // Block can be reached by a double indirect pointer
  else if (index - (REFS_PER_INODE - 3) - REFS_PER_BLOCK < REFS_PER_BLOCK * REFS_PER_BLOCK){
    b = snapshot.inode->refs[REFS_PER_INODE - 2];
    ref_block = REFS_PER_INODE - 2;
    index -= REFS_PER_INODE - 3 + REFS_PER_BLOCK;
    nlevels = 2;
  }
  // Block can be reached by the triple indirect pointer
  else {
    b = snapshot.inode->refs[REFS_PER_INODE - 1];
    ref_block = REFS_PER_INODE - 1;
    index -= REFS_PER_INODE - 3 + REFS_PER_BLOCK*REFS_PER_BLOCK;
    nlevels = 3;
  }

  // the indirect block has to be allocated
  if (b == 0 && nlevels) {
    block_no free_block_no;
    write_next_free_block(bi, &null_block, &free_block_no);
    // update inode and write back
    snapshot.inode->refs[ref_block] = free_block_no;
    (*us->below->write)(us->below, snapshot.inode_blockno, (block_t *) &snapshot.inodeblock.inodeblock);
    b = free_block_no;
  }
  
  struct ufs_indirblock indir;
  // Traverse the tree and write the intermediate indirect blocks
  int j;
  unsigned int prev_index = index;
  // nlevels is the no. of levels of indirect
  while (nlevels) {
    nlevels--;
    if (b == 0) {
      block_no free_block_no;
      write_next_free_block(bi, &null_block, &free_block_no);
      // update the previous block and write it back
      indir.refs[prev_index] = free_block_no;
      (*us->below->write)(us->below, ref_block, (block_t *) &indir);
      (*us->below->read)(us->below, free_block_no, (block_t *) &indir);	  	
      b = free_block_no;
    }
    else {
      if ((*us->below->read)(us->below, b, (block_t *) &indir) < 0){
	return -1;
      }
    }
    
    ref_block = b;
    int divider = 1;
    // Divide the offset by REFS_PER_BLOCK ^ nlevels
    for (j = nlevels; j > 0; j--) {
    	divider *= REFS_PER_BLOCK;
    }
    
    //index %= REFS_PER_BLOCK;
    b = indir.refs[index / divider];
    prev_index = index / divider;
    index %= divider;  

  }

  int res = 0;

  // Write the data block and modify structures accordingly
  if (b == 0) {
    block_no free_block_no;
    write_next_free_block(bi, block, &free_block_no);
    // If the data block was written on a direct pointer
    if (offset < REFS_PER_INODE - 3) {
      snapshot.inode->refs[offset] = free_block_no;
    }
    // If the data block was written on an indirect pointer
    else {
      indir.refs[prev_index] = free_block_no;
      res = (*us->below->write)(us->below, ref_block, (block_t *) &indir);	  
    }
    // update nblocks 
    snapshot.inode->nblocks = (offset + 1 > snapshot.inode->nblocks) ? offset + 1 : snapshot.inode->nblocks;
    res = (*us->below->write)(us->below, snapshot.inode_blockno, (block_t *) &snapshot.inodeblock.inodeblock);
  }
  else {
    res = (*us->below->write)(us->below, b, block);
  }
  return res;
}

static void ufsdisk_destroy(block_if bi){
  free(bi->state);
  free(bi);
}

/* Create or open a new virtual block store at the given inode number.
 */
block_if ufsdisk_init(block_if below, unsigned int inode_no){	
  struct ufs_snapshot snapshot;

  if(ufsdisk_get_snapshot(&snapshot, below, inode_no) < 0){
    return NULL;
  }

  //Create a block state for this inode
  struct ufs_state *us = (struct ufs_state *) malloc(sizeof(struct ufs_state));
  us->below = below;
  us->inode_no = inode_no;

  //Create a block interface for this inode
  block_if bi = calloc(1, sizeof(*bi));
  bi->state = us;
  bi->nblocks = ufsdisk_nblocks;
  bi->read = ufsdisk_read;
  bi->write = ufsdisk_write;
  bi->setsize = ufsdisk_setsize;
  bi->destroy = ufsdisk_destroy;

  return bi;
}

/*************************************************************************
 * The code below is for creating new ufs-like file systems.  This should
 * only be invoked once per underlying block store.
 ************************************************************************/

/* Create the freebitmap blocks adjacent to ufs_inodeblocks, and return the number of 
 * freebitmap blocks created.
 *
 * The number of ufs_freebitmap blocks (f) can be estimated using:
 *
 * f <= K / (1 + BLOCK_SIZE * 2^3), where K is as follows:
 * K = nblocks - 1 - ceil(n_inodes/INODES_PER_BLOCK)
 */
block_no setup_freebitmapblocks(block_if below, block_no next_free, block_no nblocks)
{
  if (below == NULL){
    fprintf(stderr,"!!UFSERR: underlying file system not initialized\n");
    return -1;
  }

  int K = nblocks - next_free;
  block_no n_freebitmapblocks = K / (1 + BLOCK_SIZE * 8);	// # freebitmap blocks
  int i;
  for (i = next_free; i <= n_freebitmapblocks; i++) {
    if ((*below->write)(below, i, &null_block) < 0) {
      return -1;
    }
  }
  
  return n_freebitmapblocks;
}

/* Create a new file system on the block store below.
 */
int ufsdisk_create(block_if below, unsigned int n_inodes, unsigned int magic_number){
  if (sizeof(union ufs_block) != BLOCK_SIZE) {
    fprintf(stderr,"ufsdisk_create: block has wrong size\n");
  }

  unsigned int n_inodeblocks =
    (n_inodes + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK;
  int nblocks = (*below->nblocks)(below);
  if (nblocks < n_inodeblocks + 2) {
    fprintf(stderr, "ufsdisk_create: too few blocks\n");
    return -1;
  }

  /* Initialize the superblock.
   */
  union ufs_block superblock;
  memset(&superblock, 0, BLOCK_SIZE);
  superblock.superblock.magic_number = magic_number;
  superblock.superblock.n_inodeblocks = n_inodeblocks;
  superblock.superblock.n_freebitmapblocks =
    setup_freebitmapblocks(below, n_inodeblocks + 1, nblocks);
  if ((*below->write)(below, 0, (block_t *) &superblock) < 0) {
    return -1;
  }

  /* The inodes all start out empty.
   */
  int i;
  for (i = 1; i <= n_inodeblocks; i++) {
    if ((*below->write)(below, i, &null_block) < 0) {
      return -1;
    }
  }

  return 0;
}

/*
 * Write the block to next available free block
 */

static int write_next_free_block(block_if bi, block_t *write_block, block_no *free_block_no)
{
  struct ufs_state *us = bi->state;
  struct ufs_snapshot snapshot;

  if (ufsdisk_get_snapshot(&snapshot, us->below, us->inode_no) < 0){
    return -1;
  }

  unsigned int start = snapshot.superblock.superblock.n_inodeblocks + 1;
  unsigned int end = start + snapshot.superblock.superblock.n_freebitmapblocks;
  block_no b = end - 1;	       
  block_t block;
  unsigned int i, j;
  unsigned char k;
  for (i = start; i < end; i++) {
    (*us->below->read)(us->below, i, &block);
    for (j = 0; j < BLOCK_SIZE; j++) {
      if (block.bytes[j] == 255) {
		b += 8;
		continue;
      }
      for (k = 128; k != 0; k = k>>1) {
	b += 1;
	if ((block.bytes[j] & k) == 0) {
	  block.bytes[j] |= k;		
	  if ((*us->below->write)(us->below, i, &block) < 0)
	    return -1;
	  *free_block_no = b;
	  if ((*us->below->write)(us->below, b, write_block) < 0)
	    return -1;
	  return 0;
	}
      }
    }
  }
  return -1;
}

/*
 * Set the specified block free
 */

static int set_block_free(block_if bi, block_no b)
{
  //printf("freeing block %u\n", b);
  struct ufs_state *us = bi->state;
  struct ufs_snapshot snapshot;
  
  if (ufsdisk_get_snapshot(&snapshot, us->below, us->inode_no) < 0){
    return -1;
  }

  unsigned int start = snapshot.superblock.superblock.n_inodeblocks + 1;
  unsigned int sub = snapshot.superblock.superblock.n_inodeblocks + 1 + snapshot.superblock.superblock.n_freebitmapblocks;
  int free_block_no = start + (b - sub) / (BLOCK_SIZE * 8);
  int free_block_char = ((b-sub) / (8)) % BLOCK_SIZE;
  int free_block_bit = (b-sub) % 8;
  block_t block;
  if((*us->below->read)(us->below, free_block_no, &block) < 0) {
    return -1;
  }
  char val = 128 >> free_block_bit;
  block.bytes[free_block_char] ^= val;
  if ((*us->below->write)(us->below, free_block_no, &block) < 0)
    return -1;
  return 0;
}
