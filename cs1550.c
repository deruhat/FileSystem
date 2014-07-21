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

#ifndef DEBUGFILEWRITE
#define DEBUGFILEWRITE 0
#endif

#ifndef DEBUGFILEREAD
#define DEBUGFILEREAD 0
#endif

#ifndef DEBUGALLOCATE
#define DEBUGALLOCATE 0
#endif
//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define	MAX_FILES_IN_DIR (BLOCK_SIZE - (MAX_FILENAME + 1) - sizeof(int)) / \
	((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

// 5MB / 512 byte block = 10240 blocks on our disk. Use a bit less than that for safety's sake in determining size of disk
#define BLOCKS_ON_DISK 10240
	
//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(size_t) - sizeof(long))

#define DISK_MANAGEMENT_FILLER (BLOCK_SIZE - 2*sizeof(int) - sizeof(long))

struct cs1550_disk_management
{
	int prevAllocations;	// Marks whether any allocations have been made: 0 if not, 1 is so
	long free;			// First block that is free
		
	char filler[DISK_MANAGEMENT_FILLER]; // rest of the block is just an empty array to ensure that this struct is 1 block
};

typedef struct cs1550_disk_management cs1550_disk_management;

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
	
	size_t size; // Stores how many bytes are used in this block

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
	
	#if DEBUGFILE
	printf("Beginning getattr\n");
	#endif
	
	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} 
	
	else 
	{
		#if DEBUGFILE
		printf("Scanning path\n");
		#endif
		res = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		
		
		if(res == EOF) // Some error occurred when scanning path
			return -ENOENT;
		else;
		
		//Check if name is subdirectory
		//All files should have extensions, if one is lacking then this is a directory
		if(res < 2) 
		{
				cs1550_directory_entry *entry = malloc(sizeof(cs1550_directory_entry));
				#if DEBUGFILE
				printf("Opening file .directories\n");
				#endif
				FILE *f = fopen(".directories", "rb");
				int directoryFound = 0;
				if(f == NULL)
				{
					#if DEBUGFILE
					printf(".directories does not exist, returning -ENOENT\n");
					#endif
					free(entry);
					return -ENOENT;
				}
				else
				{
					#if DEBUGFILE
					printf(".directories exists, searching for directory\n");
					#endif
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
	
		if(strlen(directory) >= 9) 
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
	//(void) offset;
	(void) fi;
	//(void) path;
	
	int len = strlen(path);
	char* directory = malloc(len);
	char* filename = malloc(len);
	char* extension = malloc(len);
	int res = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	int i = 0;
	int found = 0;
	cs1550_directory_entry *dir = malloc(sizeof(cs1550_directory_entry));
	struct cs1550_file_directory *dirFile = malloc(sizeof(struct cs1550_file_directory));
	long sizeRead = 0;
	long sizePassed = 0;
	int sizeReturn = 0;
	
	#if DEBUGFILEREAD
	printf("Size to read is %d\n", size);
	#endif
	
	//check to make sure path exists
	if(res < 2)
	{
		free(directory);
		free(filename);
		free(extension);
		return -EISDIR;
	}
	else;
	
	FILE *directories = fopen(".directories", "rb");
	FILE *disk = fopen(".disk", "rb");
	
	if(directories == NULL || disk == NULL)
	{
		free(directory);
		free(filename);
		free(extension);
		free(dir);
		free(dirFile);
	}
	else;
	
	while(found < 1 && fread(dir, sizeof(cs1550_directory_entry), 1, directories) != 0)
	{
		if(strcmp(dir->dname, directory) == 0)
			found = 1;
		else;
	}
	
	found = 0;
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
	//check that size is > 0
	//check that offset is <= to the file size
	if(offset <= dirFile->fsize && size > 0)
	{
		long nextBlock = dirFile->nStartBlock;
		int moreBlocks = 1;
		long runningOffset = offset;
		long fsize = dirFile->fsize;
		int blockSizeToRead;
		cs1550_disk_block *block = malloc(sizeof(cs1550_disk_block));
		
		//read in data
		#if DEBUGFILEREAD
		printf("Beginning read loop\n");
		#endif
		while(moreBlocks == 1)
		{
			#if DEBUGFILEREAD
			printf("Seeking to block\n");
			#endif
			fseek(disk, nextBlock * BLOCK_SIZE, SEEK_SET);
			fread(block, sizeof(cs1550_disk_block), 1, disk);
			if(runningOffset > MAX_DATA_IN_BLOCK)
			{
				#if DEBUGFILEREAD
				printf("Not yet at offset\n");
				#endif
				runningOffset -= MAX_DATA_IN_BLOCK; // If our offset does not start in this block then we just go to the next block
			}
			else
			{
				if(runningOffset > 0)
				{	
					blockSizeToRead = size - sizeRead;
					#if DEBUGFILEREAD
					printf("Reading offset block\n");
					#endif
					// If we're at the block for offset then only read from there, and after that just read the entirety of any full block
					
					if(blockSizeToRead + runningOffset < block->size) // If we want to read less than all of the block's data
					{
						#if DEBUGFILEREAD
						printf("Reading %d blocks from file, reaching count\n", blockSizeToRead);
						#endif
						memcpy((buf + sizeRead), (block->data + runningOffset), (blockSizeToRead));
						sizeRead += blockSizeToRead;
					}
					else
					{
						#if DEBUGFILEREAD
						printf("Reading %d bytes from block\n", block->size);
						#endif
						memcpy((buf + sizeRead), (block->data + runningOffset), block->size - runningOffset);
						sizeRead += block->size - runningOffset;
					}
					runningOffset = 0;
				}
				else
				{
					blockSizeToRead = size - sizeRead;
					#if DEBUGFILEREAD
					printf("Reading non-offset block\n");
					#endif
					// If no offset, or if we already read past the offset and are in a new block, then we just read whole blocks
					
					if(blockSizeToRead < block->size) // If we want to read less than all of the block's data
					{
						#if DEBUGFILEREAD
						printf("Reading %d blocks from file, reaching count\n", blockSizeToRead);
						#endif
						memcpy((buf + sizeRead), block->data, blockSizeToRead);
						sizeRead += blockSizeToRead;
					}
					else
					{
						#if DEBUGFILEREAD
						printf("Reading %d bytes from block\n", block->size);
						#endif
						memcpy((buf + sizeRead), block->data, block->size);
						sizeRead += block->size;
					}
				}
				
			}
		
			sizePassed += block->size;
			
			// If more to read and there is another block in the link then keep reading
			if(size > sizeRead && block->nNextBlock > 0)
			{
				nextBlock = block->nNextBlock;
				#if DEBUGFILEREAD
				printf("Setting next block to %d\n", nextBlock);
				#endif
				moreBlocks = 1;
			}
			// If we have read the whole file or for whatever reason there is no next block linked, then we're done
			else
			{
				#if DEBUGFILEREAD
				printf("End of file\n");
				#endif	
				nextBlock = -1;
				moreBlocks = 0;
			}
			
		}
		free(block);
	
	}
	else;
	
	
	
	free(directory);
	free(filename);
	free(extension);
	free(dir);
	free(dirFile);
	fclose(directories);
	fclose(disk);
	
	return sizeRead;
}

// Allocates a new block in the .disk file, returning the block number that has been allocated
static long allocateDisk()
{
	#if DEBUGFILE
	printf("Beginning allocateDisk()\n");
	#endif
	FILE *f = fopen(".disk", "rb+");
	cs1550_disk_management *manage = malloc(sizeof(cs1550_disk_management));
	long blockAllocated;
	fread(manage, sizeof(cs1550_disk_management), 1, f);
	
	if(manage->prevAllocations == 0)
	{
		#if DEBUGFILE
		printf("Performing first allocation\n");
		#endif
		manage->prevAllocations = 1;
		manage->free = 2; // 0th block is for disk management struct, so we need to allocate the first block
		blockAllocated = 1;
	}
	else
	{
		#if DEBUGFILE
		printf("Performing non-first allocation\n");
		#endif
		blockAllocated = manage->free;
		if(blockAllocated >= BLOCKS_ON_DISK - 1)
		{
			#if DEBUGALLOCATE
			printf("NO MORE SPACE FOR ALLOCATION\n");
			#endif
			blockAllocated = -1;
		}
		else
		{
			manage->free++;
		}
	}
	fseek(f, -sizeof(cs1550_disk_management), SEEK_CUR);
	fwrite(manage, sizeof(cs1550_disk_management), 1, f);
	fclose(f);
	#if DEBUGFILE
	printf("allocateDisk() has finished, returning\n");
	#endif
	return blockAllocated;
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
	
	int len = strlen(path);
	char* directory = malloc(len);
	char* filename = malloc(len);
	char* extension = malloc(len);
	int res = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	int i = 0;
	int found = 0;
	long sizeWritten = 0;
	cs1550_directory_entry *dir = malloc(sizeof(cs1550_directory_entry));
	struct cs1550_file_directory *dirFile = malloc(sizeof(struct cs1550_file_directory));
	
	#if DEBUGFILE
	printf("Beginning write\n");
	#endif
	
	if(res < 2)
	{
		free(directory);
		free(filename);
		free(extension);
		return 0;
	}
	else;
	
	#if DEBUGFILE
	printf("Opening files\n");
	#endif
	FILE *directories = fopen(".directories", "rb+");
	FILE *disk = fopen(".disk", "rb+");
	
	if(directories == NULL || disk == NULL)
	{
		#if DEBUGFILE
		printf(".directories or .disk does not seem to exist, exiting\n");
		#endif
		free(directory);
		free(filename);
		free(extension);
		free(dir);
		free(dirFile);
		return 0;
	}
	else;
	
	#if DEBUGFILE
	printf("Searching for directory\n");
	#endif
	while(found < 1 && fread(dir, sizeof(cs1550_directory_entry), 1, directories) != 0)
	{
		if(strcmp(dir->dname, directory) == 0)
			found = 1;
		else;
	}
	
	found = 0;
	#if DEBUGFILE
	printf("Searching for file\n");
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
	//check that size is > 0
	//check that offset is <= to the file size
	#if DEBUGFILE
	printf("Checking to ensure offset is within file size\n");
	#endif
	if(offset > dirFile->fsize)
	{
		free(directory);
		free(filename);
		free(extension);
		free(dir);
		free(dirFile);
		fclose(directories);
		fclose(disk);
		return -EFBIG;
	}
	//write data
	else
	{
		long nextBlock = dirFile->nStartBlock;
		int moreBlocks = 1;
		long runningOffset = offset;
		int writtenToBlock = 0; // amount of bytes written to a given block
		cs1550_disk_block *block = malloc(sizeof(cs1550_disk_block));
		
		#if DEBUGFILE
		printf("Checking to see if file needs to be created on disk/n");
		#endif
	
		if(dirFile->nStartBlock == -1)
		{
			#if DEBUGFILE
			printf("Allocating initial disk space for file\n");
			#endif
			fclose(disk);
			dirFile->nStartBlock = allocateDisk();
			disk = fopen(".disk", "rb+");
			#if DEBUGFILEWRITE
			printf("Allocated block at %ld\n", dirFile->nStartBlock);
			#endif
			if(dirFile->nStartBlock < 0)
			{
				moreBlocks = 0; // No more space on disk, no writes shall occur
			}
			else;
		}
		else;
		
		nextBlock = dirFile->nStartBlock;
		//write data
		#if DEBUGFILE
		printf("Beginning write loop\n");
		#endif
		while(moreBlocks == 1)
		{
			#if DEBUGFILEWRITE
			printf("Seeking to block\n");
			#endif
			fseek(disk, BLOCK_SIZE * nextBlock, SEEK_SET);
			fread(block, sizeof(cs1550_disk_block), 1, disk);
			if(runningOffset > MAX_DATA_IN_BLOCK)
			{
				#if DEBUGFILEWRITE
				printf("Have not yet reached offset\n");
				#endif
				nextBlock = block->nNextBlock;
				runningOffset -= MAX_DATA_IN_BLOCK; // If our offset does not start in this block then we just go to the next block
			}
			else
			{
				if(runningOffset > 0)
				{	
					#if DEBUGFILEWRITE
					printf("Writing to offset block\n");
					#endif
					if((size - sizeWritten + runningOffset) < MAX_DATA_IN_BLOCK)
					{
						writtenToBlock = size - sizeWritten + runningOffset;
						#if DEBUGFILEWRITE
						printf("Writing less than full block\nMallocing space for writing\n");
						printf("Using memcpy to move information from buffer to block data\n");
						#endif
						memcpy((block->data + runningOffset), (buf + sizeWritten), (size - sizeWritten));
						if(block->size < writtenToBlock) // If we appended anything to the block then we need to change block size.
							block->size = writtenToBlock;
						else;
						#if DEBUGFILEWRITE
						printf("Increasing size written by %d\n", size-sizeWritten);
						#endif
						sizeWritten += size - sizeWritten;
					}
					else
					{
						#if DEBUGFILEWRITE
						printf("Writing rest of block from offset of %d\n", runningOffset);
						#endif
						memcpy((block->data + runningOffset), (buf + sizeWritten), (MAX_DATA_IN_BLOCK-runningOffset));
						#if DEBUGFILEWRITE
						printf("Increasing block size and sizeWritten\n");
						#endif
						block->size = MAX_DATA_IN_BLOCK;
						sizeWritten += MAX_DATA_IN_BLOCK - runningOffset;
					}
					runningOffset = 0;	
				}
				else
				{
					#if DEBUGFILEWRITE
					printf("Writing to non-offset block\n");
					#endif
					if((size - sizeWritten) < MAX_DATA_IN_BLOCK)
					{
						writtenToBlock = size - sizeWritten;
						#if DEBUGFILEWRITE
						printf("Writing less than full block\n");
						printf("Using memcpy to move information from buffer to block data\n");
						printf("Writing %d bytes\n", writtenToBlock);
						#endif
						memcpy(block->data, (buf + sizeWritten), writtenToBlock);
						if(block->size < writtenToBlock) // If we appended anything to the block then we need to change block size.
							block->size = writtenToBlock;
						else;
						#if DEBUGFILEWRITE
						printf("Increasing size written by %d\n", size-sizeWritten);
						#endif
						sizeWritten += size - sizeWritten;
					}
					else
					{
						#if DEBUGFILEWRITE
						printf("Writing full block data from buffer\n");
						#endif
						memcpy(block->data, (buf + sizeWritten), MAX_DATA_IN_BLOCK);
						
						#if DEBUGFILEWRITE
						printf("Increasing block size and sizeWritten\n");
						#endif
						block->size = MAX_DATA_IN_BLOCK;
						sizeWritten += MAX_DATA_IN_BLOCK;
					}
				}
				
				#if DEBUGFILEWRITE
				printf("Finished writing block, block size is now %d\n", block->size);
				#endif
				
				if(sizeWritten >= size) // If we've written all that was requested we're done
				{
					#if DEBUGFILEWRITE
					printf("Written requested amount, loop ending\n");
					#endif
					moreBlocks = 0;
					nextBlock = -1;
				}
				else
				{
					#if DEBUGFILEWRITE
					printf("More to write, setting next block\n");
					#endif
					moreBlocks = 1;
					if(block->nNextBlock == 0) // need to allocate new space
					{
						#if DEBUGFILEWRITE
						printf("No next block detected, allocating new space\n");
						#endif
						fclose(disk);
						disk = fopen(".disk", "rb+");
						fseek(disk, BLOCK_SIZE * (nextBlock + 1), SEEK_SET);
						block->nNextBlock = allocateDisk();
					}
					else;
					#if DEBUGFILEWRITE
					printf("Next block has been set as %ld\n", block->nNextBlock);
					#endif
					if(block->nNextBlock < 0)
					{
						#if DEBUGFILEALLOCATE
						printf("No more space for allocation\n");
						#endif
						moreBlocks = 0; // Out of space on disk, no more writes
					}
					else
						nextBlock = block->nNextBlock;
				}
				
				#if DEBUGFILEWRITE
				printf("Seeking backwards to write block\n");
				#endif
				fseek(disk, -sizeof(cs1550_disk_block), SEEK_CUR);
				#if DEBUGFILEWRITE
				printf("Writing block\n");
				#endif
				fwrite(block, sizeof(cs1550_disk_block), 1, disk);
			}
		}
		free(block);
	}
	if(dirFile->fsize < offset + sizeWritten) // size of file only changes if we wrote past the old end of file
		dirFile->fsize = offset + sizeWritten;
	else;
	*(dir->files + i) = *dirFile;
	fseek(directories, -sizeof(cs1550_directory_entry), SEEK_CUR);
	fwrite(dir, sizeof(cs1550_directory_entry), 1, directories);
	
	free(directory);
	free(filename);
	free(extension);
	free(dir);
	free(dirFile);
	fclose(directories);
	fclose(disk);
	
	//set size (should be same as input) and return, or error
	if(sizeWritten < size)
		return sizeWritten;
	else
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
