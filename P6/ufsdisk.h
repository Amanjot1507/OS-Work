/* Author: Adem Efe Gencer, November 2015.
 *
 * This file describes the layout of a UFS-like file system.  A file is
 * a virtualized block store.  Each virtualized file is identified by a
 * so-called "inode number", which indexes into an array of inodes.
 *
 * The superblock maintains the magic number, the number of inode blocks,
 * and the number of freebitmap blocks. A magic number helps recognizing a
 * legitimate file system by identifying it via a sequence of bytes.
 *
 * Each ufs_inode contains the number of blocks in the virtual block
 * store, and is filled with REFS_PER_INODE block pointers:
 *     1. The top (REFS_PER_INODE - 3) pointers are direct pointers. A
 *     direct pointer points to a datablock.
 *     2. (REFS_PER_INODE - 2)th pointer is a singly indirect pointer: 
 *     points to an indirblock that then point to datablock(s).
 *     3. (REFS_PER_INODE -1)th pointer is a doubly indirect pointer:
 *     points to an indirblock that points to other indirblock(s) that
 *     then point to datablock(s).
 *     4. (REFS_PER_INODE)th pointer is a triply indirect pointer:
 *     points to an indirblock that points to other indirblock(s) that
 *     points to other indirblock(s) then point to datablock(s).
 *
 * A freebitmap block is a block of bits that is used to keep track of free
 * blocks in the file system. Each bit in a freebitmap block corresponds to
 * a block in the "remaining blocks" section of the layout. A value of 0
 * indicates that the block is not used, and a value of 1 indicates that the
 * block is in use. Freebitmap blocks do not keep bits to track the status
 * of superblock, inodes, or the other freebitmap blocks.
 */

#define INODES_PER_BLOCK	(BLOCK_SIZE / sizeof(struct ufs_inode))
#define REFS_PER_BLOCK		(BLOCK_SIZE / sizeof(block_no))
#define REFS_PER_INODE		15
#define MAGIC_NUMBER		0xEFE0

/* Contents of the "superblock". There is only one of these.
 */
struct ufs_superblock {
	unsigned int magic_number;		// magic number of the file system
	block_no n_inodeblocks;			// # ufs_inodeblocks
	block_no n_freebitmapblocks;	// # freebitmap blocks
};

/* An inode describes a file (= virtual block store).  "nblocks" contains
 * the number of blocks in the file, while "refs" is filled with block
 * pointers. Note that initially "all files exist" but are of
 * length 0. Keeping track which files are free or not is maintained elsewhere.
 */
struct ufs_inode {
	block_no nblocks;			// total size of the file
	block_no refs[REFS_PER_INODE];
};

/* An inode block is filled with inodes.
 */
struct ufs_inodeblock {
	struct ufs_inode inodes[INODES_PER_BLOCK];
};

/* A freebitmap block is filled with status bits representing whether a block
 * in the "remaining blocks" section of the layout is free (0) or in use (1). 
 */
struct ufs_freebitmapblock {
	char status[BLOCK_SIZE];
};

/* An indirect block is an internal node rooted at an inode.
 */
struct ufs_indirblock {
	block_no refs[REFS_PER_BLOCK];
};

/* A convenient structure that's the union of all block types.  It should
 * have size BLOCK_SIZE, which may not be true for the elements.
 */
union ufs_block {
	block_t datablock;
	struct ufs_superblock superblock;
	struct ufs_inodeblock inodeblock;
	struct ufs_freebitmapblock freebitmapblock;
	struct ufs_indirblock indirblock;
};
