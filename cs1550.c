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

#ifndef DEBUGFILE
#define DEBUGFILE 0
#endif
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
	int len = strlen(path);
	char* directory = malloc(len);
	char* filename = malloc(len);
	char* extension = malloc(len);

	memset(stbuf, 0, sizeof(struct stat));
   
	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} 
	
	else 
	{
	
		res = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		
		
		if(res == EOF) // Some error occurred when scanning path
			return -ENOENT;
		else;
		
		//Check if name is subdirectory
		//All files should have extensions, if one is lacking then this is a directory
		if(res < 2) 
		{
				cs1550_directory_entry *entry = malloc(sizeof(cs1550_directory_entry));
				FILE *f = fopen(".directories", "rb");
				int directoryFound = 0;
				if(f == NULL)
				{
					fclose(f);
					free(entry);
					return -ENOENT;
				}
				else
				{
					while(directoryFound < 1 && fread(entry, sizeof(cs1550_directory_entry), 1, f) > 0)
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
					
		}
		
		else // More than directory is present in path, must be a file
		{
			
			FILE *f = fopen(".directories", "rb");
			cs1550_directory_entry *entry = malloc(sizeof(cs1550_directory_entry));
			struct cs1550_file_directory *dirFile = malloc(sizeof(struct cs1550_file_directory));
			int found = 0;
			int i = 0;
			if(f == NULL)
			{
				free(dirFile);
				free(entry);
				return -ENOENT;
			}
			else;
			
			while(found < 1 && fread(entry, sizeof(cs1550_directory_entry), 1, f) > 0)
			{
				if(strcmp(entry->dname, directory) == 0)
					found = 1;
				else;
			}
			if(found < 1) // directory doesn't exist
			{
				free(dirFile);
				free(entry);
				fclose(f);
				return -ENOENT;
			}
			else;
			
			found = 0;
			while(i < entry->nFiles && found < 1)
			{
				*dirFile = *(entry->files + i);
				if(strcmp(dirFile->fname, filename) == 0)
				{
					if(res == 2) // no extension on file
					{
						if(strcmp(dirFile->fext, "") == 0)
							found = 1;
						else
							i++;
					}
					else // filename and extension present
					{
						if(strcmp(dirFile->fext, extension) == 0)
							found = 1;
						else
							i++;
					}
				}
				else
					i++;
			}
			
			if(found < 1) // file doesn't exist
			{
				free(dirFile);
				free(entry);
				fclose(f);
				return -ENOENT;
			}
			else;
			//regular file, probably want to be read and write
			stbuf->st_mode = S_IFREG | 0666; 
			stbuf->st_nlink = 1; //file links
			stbuf->st_size = dirFile->fsize; //file size - make sure you replace with real size!
			res = 0; // no error
			
			free(dirFile);
			free(entry);
			fclose(f);
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

	int len = strlen(path);
	char* directory = malloc(len);
	char* filename = malloc(len);
	char* extension = malloc(len);
	int res = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	
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
		if(f != NULL)
		{
			cs1550_directory_entry *entry = malloc(sizeof(cs1550_directory_entry));
			while(fread(entry, sizeof(cs1550_directory_entry), 1, f) != 0)
				filler(buf, entry->dname, NULL, 0);
			free(entry);
			fclose(f);
		}
		else;	
		
	}
	
	else // need to show all files within this subdirectory
	{
		FILE *f = fopen(".directories", "rb");
		if(f != NULL)
		{
			int directoryFound = 0;
			cs1550_directory_entry *entry = malloc(sizeof(cs1550_directory_entry));
			while(directoryFound < 1 && fread(entry, sizeof(cs1550_directory_entry), 1, f) != 0)
			{	
				if(strcmp(entry->dname, directory) == 0)
					directoryFound = 1;
				else;
			}
			if(directoryFound < 1) // If we never found a subdirectory matching the one given return error
			{
				fclose(f);
				free(entry);
				return -ENOENT;
			}
			else
			{
				struct cs1550_file_directory *dirFile = malloc(sizeof(struct cs1550_file_directory));
				int i = 0;
				char fileName[20];
				while(i < entry->nFiles)
				{
					*dirFile = *(entry->files + i);
					strcpy(fileName, dirFile->fname);
					if(strcmp(dirFile->fext, "") != 0) // If file has an extension then include that when giving its name
					{
						strcat(fileName, ".");
						strcat(fileName, dirFile->fext);
					}
					else;
					filler(buf, fileName, NULL, 0);
					i++;
				}
				free(dirFile);
				free(entry);
				fclose(f);
			}
		}
		else
		{
			fclose(f);
			return -ENOENT;
		}
	}
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
	
	int len = strlen(path);
	char* directory = malloc(len);
	char* filename = malloc(len);
	char* extension = malloc(len);
	
	
	
	int res = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	
	//Check to make sure new directory is only under root
	if(strcmp(path, "/") == 0) // No new directory given
		 return -EEXIST;
	
	else if(res > 1) // Tried to make a subdirectory under something other than root
		return -EPERM;
	
	else;
	{
		int extensionTest = sscanf(path, "/%[^.].%s", filename, extension);
		
		if(extensionTest > 1) // Test to see if user tried to make something with an extension rather than a directory
			return -EPERM;
		else 
		{
			char* p;
			p = strchr(path, '.'); // Even if there's no extension directory name might have an invalid '.' in it
			if(p != NULL)
				return -EPERM;
			else;
		}
	
		if(strlen(directory) > 9) 
			return -ENAMETOOLONG;
		else
		{
			// Since we're opening in append+ we don't need to worry about if .directories doesn't exist
			FILE *f = fopen(".directories", "ab+");
			cs1550_directory_entry *entry = malloc(sizeof(cs1550_directory_entry));
			int directoryFound = 0;
			
			while(directoryFound < 1 && fread(entry, sizeof(cs1550_directory_entry), 1, f) > 0)
			{
				if(strcmp(entry->dname, directory) == 0)
				{
					directoryFound = 1;
				}
				else;
			}

			
			if(directoryFound > 0) // Directory already exists
			{
				fclose(f);
				free(entry);
				return -EEXIST;
			}
			else
			{
				cs1550_directory_entry *newDirectory;
				newDirectory = malloc(sizeof(cs1550_directory_entry));
				strcpy(newDirectory->dname, directory);
				newDirectory->nFiles = 0;
				fwrite(newDirectory, sizeof(cs1550_directory_entry), 1, f);
				fclose(f);
				free(newDirectory);
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
	
	#if DEBUGFILE
	printf("Beginning mknod\n");
	#endif
	int len = strlen(path);
	char* directory = malloc(len);
	char* filename = malloc(len);
	char* extension = malloc(len);
	int res = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	if(res < 2) // No filename given, tried to create a file in root
		return -EPERM;
	else;
	
	#if DEBUGFILE
	printf("Checking if name is too long\n");
	printf("Directory given was %s\n", directory);
	printf("Name given was %s\nExtension given was %s\n", filename, extension);
	printf("Length of filename was %d, length of extension was %d\n", strlen(filename), strlen(extension));
	#endif
	
	if(strlen(filename) > 8) // Filename was over the 8 char limit
	{
		#if DEBUGFILE
		printf("Filename too long\n");
		#endif
		return -ENAMETOOLONG;
	}
	else if(res > 2 && strlen(extension) > 3) // File name included an extension over the 3 char limit
	{
		#if DEBUGFILE
		printf("Extension too long\n");
		#endif
		return -ENAMETOOLONG;
	}
	else
	{
		
		#if DEBUGFILE
		printf("Filename good, opening .directories\n");
		#endif
		
		FILE *f = fopen(".directories", "rb+");
		if(f == NULL)
			return -EPERM;
		else;
		cs1550_directory_entry *dir = malloc(sizeof(cs1550_directory_entry));
		struct cs1550_file_directory *dirFile = malloc(sizeof(struct cs1550_file_directory));
		int i = 0;
		int found = 0;
		#if DEBUGFILE
		printf("Searching for directory to create file in\n");
		#endif
		while(found < 1 && fread(dir, sizeof(cs1550_directory_entry), 1, f) != 0)
		{
			if(strcmp(dir->dname, directory) == 0)
				found = 1;
			else;
		}
		
		found = 0;
		#if DEBUGFILE
		printf("Directory found, searching to see if file exists\n");
		#endif
		while(found < 1 && i < dir->nFiles)
		{
			*dirFile = *(dir->files + i);
			if(strcmp(dirFile->fname, filename) == 0)
			{
				if(res == 2) // no extension on file
				{
					if(strcmp(dirFile->fext, "") == 0)
						found = 1;
					else
						i++;
				}
				else // filename and extension present
				{
					if(strcmp(dirFile->fext, extension) == 0)
						found = 1;
					else
						i++;
				}
			}
			else
				i++;
		}
		
		if(found > 0)
		{
			fclose(f);
			free(dir);
			free(dirFile);
			return -EEXIST;
		}
		else;
		
		#if DEBUGFILE
		printf("File does not exist, creating file\n");
		printf("Directory has %d files, making file in %d index of array\n", dir->nFiles, i);
		#endif
		
		strcpy(dirFile->fname, filename);
		
		if(res == 3)
			strcpy(dirFile->fext, extension);
		else
			strcpy(dirFile->fext, "");
		
		dirFile->fsize = 0;
		dirFile->nStartBlock = -1;
		
		*(dir->files + i) = *dirFile;
		dir->nFiles += 1;
		
		fseek(f, -sizeof(cs1550_directory_entry), SEEK_CUR);
		
		fwrite(dir, sizeof(cs1550_directory_entry), 1, f);
		
		fclose(f);
		free(dir);
		free(dirFile);
		return 0;
	}
	
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

	size = 0;
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
