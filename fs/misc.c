/*************************************************************************//**
 *****************************************************************************
 * @file   misc.c
 * @brief  
 *****************************************************************************
 *****************************************************************************/

/* Orange'S FS */

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "keyboard.h"
#include "proto.h"
#include "hd.h"
#include "fs.h"

/*****************************************************************************
 *                                do_stat
 *************************************************************************//**
 * 作用是得到文件的信息，在这里我们主要关心其大小
 * 
 * @return  On success, zero is returned. On error, -1 is returned.
 *****************************************************************************/
PUBLIC int do_stat()
{
	char pathname[MAX_PATH]; /* 来自调用者的参数 */
	char filename[MAX_PATH]; /* directory has been stipped */

	/* get parameters from the message */
	int name_len = fs_msg.NAME_LEN;	/* 文件名长度 */
	int src = fs_msg.source;	/* 调用者的进程号. */
	assert(name_len < MAX_PATH);
	phys_copy((void*)va2la(TASK_FS, pathname),    /* 目的地   */
		  (void*)va2la(src, fs_msg.PATHNAME), /* 来源 */
		  name_len);
	pathname[name_len] = 0;	/* terminate the string */

	int inode_nr = search_file(pathname);
	if (inode_nr == INVALID_INODE) {	/* 文件没找到 */
		printl("{FS} FS::do_stat():: search_file() returns "
		       "invalid inode: %s\n", pathname);
		return -1;
	}

	struct inode * pin = 0;

	struct inode * dir_inode;
	if (strip_path(filename, pathname, &dir_inode) != 0) {
		/* theoretically never fail here
		 * (it would have failed earlier when
		 *  search_file() was called)
		 */
		assert(0);
	}
	pin = get_inode(dir_inode->i_dev, inode_nr);

	struct stat s;		/* 请求的东西 */
	s.st_dev = pin->i_dev;
	s.st_ino = pin->i_num;
	s.st_mode= pin->i_mode;
	s.st_rdev= is_special(pin->i_mode) ? pin->i_start_sect : NO_DEV;
	s.st_size= pin->i_size;

	put_inode(pin);

	phys_copy((void*)va2la(src, fs_msg.BUF), /* 目的地   */
		  (void*)va2la(TASK_FS, &s),	 /* 来源 */
		  sizeof(struct stat));

	return 0;
}

/*****************************************************************************
 *                                search_file
 *****************************************************************************/
/**
 * 我们还是通过strip_path( )来得到文件所在目录的i-node，通过这个i-node来得到目录
 * 所在的扇区，然后读取这些扇区，查看里面是否有我们要找的文件，如果找到就返回文件
 * 的i-node，如果没找到就返回零。
 *
 * @param[in] path The full path of the file to search.
 * @return         Ptr to the i-node of the file if successful, otherwise zero.
 * 
 * @see open()
 * @see do_open()
 *****************************************************************************/
PUBLIC int search_file(char * path)
{
	int i, j;

	char filename[MAX_PATH];
	memset(filename, 0, MAX_FILENAME_LEN);
	struct inode * dir_inode;
	if (strip_path(filename, path, &dir_inode) != 0)
		return 0;

	if (filename[0] == 0)	/* 路径 "/" */
		return dir_inode->i_num;

	/**
	 * 找到文件的目录
	 */
	int dir_blk0_nr = dir_inode->i_start_sect;
	int nr_dir_blks = (dir_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	int nr_dir_entries =
	  dir_inode->i_size / DIR_ENTRY_SIZE; /**
					       * including unused slots
					       * (the file has been deleted
					       * but the slot is still there)
					       */
	int m = 0;
	struct dir_entry * pde;
	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);
		pde = (struct dir_entry *)fsbuf;
		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) {
			if (memcmp(filename, pde->name, MAX_FILENAME_LEN) == 0)
				return pde->inode_nr;
			if (++m > nr_dir_entries)
				break;
		}
		if (m > nr_dir_entries) /* 所有目录项都已经被遍历 */
			break;
	}

	/* 文件没找到 */
	return 0;
}

/*****************************************************************************
 *                                strip_path
 *****************************************************************************/
/**
 * 把路径分成文件名和文件夹两部分
 *
 * 在当前文件系统中，只支持根目录
 * 没有子目录
 *
 * 将在文件操作的开头被调用，如open(), read() and write(). 
 * 接收一个完整的路径并返回两个东西：文件名和目录inode指针
 * two things: the basename and a ptr of the root dir's i-node.
 *
 * e.g. After stip_path(filename, "/blah", ppinode) finishes, we get:
 *      - filename: "blah"
 *      - *ppinode: root_inode
 *      - ret val:  0 (successful)
 *
 * 当前支持的路径名以“/”开头
 *
 * 文件名不能含有'/' and '\\0'.
 *
 * @param[out] filename The string for the result.
 * @param[in]  pathname The full pathname.
 * @param[out] ppinode  The ptr of the dir's inode will be stored here.
 * 
 * @return Zero if success, otherwise the pathname is not valid.
 *****************************************************************************/
PUBLIC int strip_path(char * filename, const char * pathname,
		      struct inode** ppinode)
{
	const char * s = pathname;
	char * t = filename;

	if (s == 0)
		return -1;

	if (*s == '/')
		s++;

	while (*s) {		/* 确认每一个字符 */
		if (*s == '/')
			return -1;
		*t++ = *s++;
		/* 文件名太长的话，就截断它 */
		if (t - filename >= MAX_FILENAME_LEN)
			break;
	}
	*t = 0;

	*ppinode = root_inode;

	return 0;
}

