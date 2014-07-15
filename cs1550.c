/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.

	gcc -Wall `pkg-config fuse --cflags --libs` cs1550.c -o cs1550
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define	MAX_FILES_IN_DIR (BLOCK_SIZE - (MAX_FILENAME + 1) - sizeof(int)) / \
	((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(size_t) - sizeof(long))

struct cs1550_directory_entry
{
	char dname[MAX_FILENAME	+ 1];	//the directory name (plus space for a nul)
	int nFiles;			//How many files are in this directory. 
					//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;			//file size
		long nStartBlock;		//where the first block is on disk
	} files[MAX_FILES_IN_DIR];		//There is an array of these
};

typedef struct cs1550_directory_entry cs1550_directory_entry;

struct cs1550_disk_block
{
	//Two choices for interpreting size: 
	//	1) how many bytes are being used in this block
	//	2) how many bytes are left in the file
	//Either way, size tells us if we need to chase the pointer to the next
	//disk block. Use it however you want.
	size_t size;	

	//The next disk block, if needed. This is the next pointer in the linked 
	//allocation list
	long nNextBlock;

	//And all the rest of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	char directory[9];
	char filename[9];
	char extension[4];
	char directPath[20];
	strcpy(directPath, "/");

	memset(stbuf, 0, sizeof(struct stat));
   
	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} 
	
	else 
	{
	
		res = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		
		strcat(directPath, directory);
		
		if(res == EOF) // Some error occurred when scanning path
			return -ENOENT;
		else;
		
		//Check if name is subdirectory
		//All files should have extensions, if one is lacking then this is a directory
		if(strcmp(path, directPath) == 0) // If only the root and directory are given then this must be a directory
		{
				cs1550_directory_entry *entry = malloc(sizeof(cs1550_directory_entry));
				FILE *f = fopen(".directories", "rb");
				int directoryFound = 0;
				if(f == NULL)
				{
					free(entry);
					return -ENOENT;
				}
				else
				{
					while(fread(entry, sizeof(cs1550_directory_entry), 1, f) != 0 && directoryFound < 1)
					{
						if(strcmp(entry->dname, directory) == 0)
							directoryFound = 1;
						else;
					}
					fclose(f);
					free(entry);
					if(directoryFound > 0)
					{
						stbuf->st_mode = S_IFDIR | 0755;
						stbuf->st_nlink = 2;
						res = 0; //no error
					}
					else
						return -ENOENT;
				}
			
				//Might want to return a structure with these fields
				
			
		}
		//Check if name is a regular file
		//Since an extension is present this must be a file
		else
		{
				return -ENOENT;
				//regular file, probably want to be read and write
				stbuf->st_mode = S_IFREG | 0666; 
				stbuf->st_nlink = 1; //file links
				stbuf->st_size = 0; //file size - make sure you replace with real size!
				res = 0; // no error
			
		}
		
	
	}
	return res;
}

/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;

	char directory[9];
	char filename[9];
	char extension[4];
	
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	
	//This line assumes we have no subdirectories, need to change
	/*
	if (strcmp(path, "/") != 0)
	return -ENOENT;
	*/

	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	
	if(strcmp(path, "/") == 0) // need to show all subdirectories
	{
		FILE *f = fopen(".directories", "rb");
		cs1550_directory_entry *entry = malloc(sizeof(cs1550_directory_entry));
		while(fread(entry, sizeof(cs1550_directory_entry), 1, f) != 0)
			filler(buf, entry->dname, NULL, 0);
			
		free(entry);
		fclose(f);
	}
	
	else // need to show all files within this subdirectory
	{
		FILE *f = fopen(".directories", "rb");
		int directoryFound = 0;
		cs1550_directory_entry *entry = malloc(sizeof(cs1550_directory_entry));
		while(fread(entry, sizeof(cs1550_directory_entry), 1, f) != 0 && directoryFound < 1)
		{	
			if(strcmp(entry->dname, directory) == 0)
				directoryFound = 1;
			else;
		}
		if(directoryFound < 1) // If we never found a subdirectory matching the one given return error
			return -ENOENT;
		else
		{
			struct cs1550_file_directory *dirFile = malloc(sizeof(struct cs1550_file_directory));
			int i = 0;
			while(i < entry->nFiles)
			{
				char fileName[12];
				*dirFile = *(entry->files + i);
				strcpy(fileName, dirFile->fname);
				strcat(fileName, ".");
				strcat(fileName, dirFile->fext);
				filler(buf, fileName, NULL, 0);
				i++;
			}
			free(dirFile);
			free(entry);
			fclose(f);
		}
	}
	/*
	//add the user stuff (subdirs or files)
	//the +1 skips the leading '/' on the filenames
	filler(buf, newpath + 1, NULL, 0);
	*/
	return 0;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	//(void) path;
	(void) mode;
	
	char* directory;
	char* filename;
	char* directPath;
	strcpy(directPath, "/");
	
	
	
	sscanf(path, "/%[^/]/%s", directory, filename);
	
	strcat(directPath, directory);
	//Check to make sure new directory is only under root
	if(strcmp(path, "/") == 0) // No new directory given
		 return -EEXIST;
	
	else if(strcmp(path, directPath) != 0) // Tried to make a subdirectory under something other than root
		return -EPERM;
	
	else;
	{
		if(strlen(path) > 10) // Directory name max length + the root's '/' + null terminator = 10. If path is longer than this then the name is too long
			return -ENAMETOOLONG;
		else
		{
			FILE *f = fopen(".directories", "rb");
			cs1550_directory_entry *entry = malloc(sizeof(cs1550_directory_entry));
			int directoryFound = 0;
			while(fread(entry, sizeof(cs1550_directory_entry), 1, f) > 0 && directoryFound < 1)
			{
				if(strcmp(entry->dname, directory) == 0)
				{
					directoryFound = 1;
				}
				else;
			}
			fclose(f);
			if(directoryFound > 0) // Directory already exists
			{
				free(entry);
				return -EEXIST;
			}
			else
			{
				FILE *f = fopen(".directories", "ab");
				cs1550_directory_entry *newDirectory = malloc(sizeof(cs1550_directory_entry));
				strcpy(newDirectory->dname, directory);
				newDirectory->nFiles = 0;
				fwrite(newDirectory, sizeof(cs1550_directory_entry), 1, f);
			}
		}
	}
		
	return 0;
}

/* 
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/* 
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;
	return 0;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/* 
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//read in data
	//set size and return, or error

	size = 0;
	

	return size;
}

/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, 
			  off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//write data
	//set size (should be same as input) and return, or error

	return size;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
