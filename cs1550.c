/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3
#define DISKFILE ".disk"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

#define USED 1		//Used for FAT
#define UNUSED 0	//Used for FAT
#define DEBUG 0		//Debugger

//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//Total available space/ number of blocks = Free blocks
#define MAX_NUM_BLOCKS (5000000/BLOCK_SIZE)

//Global Variables
char directory[MAX_FILENAME + 1];
char filename[MAX_FILENAME + 1];
char extension[MAX_EXTENSION + 1];

//The attribute packed means to not align these things
struct cs1550_directory_entry
{

	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;

struct cs1550_root_directory root_block;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;

//Block nStartBlock[0] used for root block
//Block nStartBlock[1] used for FAT
//If nStartBlock = 1 is used, nStartBlock = 0 is unused


struct cs1550_FAT_buf{
	int nStartBlock[MAX_NUM_BLOCKS];	//Array of 5000K/512 possible blocks
}FAT_buf;

//Function Prototypes
int get_root_block(struct cs1550_root_directory *);
int write_root_block(struct cs1550_root_directory);
int get_FAT_block(struct cs1550_FAT_buf *);
int write_FAT_block(struct cs1550_FAT_buf);
int get_free_nStartBlock(struct cs1550_FAT_buf *, int file_flag);

typedef struct cs1550_directory_entry cs1550_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE)


struct cs1550_disk_block

{
	//All of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

int get_root_block(struct cs1550_root_directory *root_block){
	FILE *fd;
	fd = fopen(DISKFILE, "r+b");
	if(fd == NULL){
		printf("Error: Unable to open disk: %s\n", DISKFILE);
		return -ENOENT;		//File not found
	}
	fread(root_block, BLOCK_SIZE, 1, fd);
	fclose(fd);
	return 0;
}
int write_root_block(struct cs1550_root_directory root_block){
	int i = 0;
	FILE *fd = fopen(DISKFILE, "r+b");			//Open disk for writing binary
	if(fd == NULL){
		printf("Error: Unable to open disk: %s\n", DISKFILE);
		return -ENOENT;		//File not found
	}

	if(DEBUG)printf("In write_root_block(), Number of directories %d\n", root_block.nDirectories);
	for(i = 0; i < root_block.nDirectories; i++){
		if(DEBUG)printf("In write_root_block(), Directory = %s\n", root_block.directories[i].dname);
		if(DEBUG)printf("nStartBlock = %ld\n", root_block.directories[i].nStartBlock);
	}
	i = fwrite(&root_block, BLOCK_SIZE, 1, fd);
	if(DEBUG)printf("In write_root_block, blocks returned: i = %d\n", i);
	fclose(fd);
	return 0;
}


int get_FAT_block(struct cs1550_FAT_buf *FAT_block){
	FILE *fd;
	fd = fopen(DISKFILE, "r+b");
	if(fd == NULL){
		printf("Error: Unable to open file in get_FAT_block: %s\n", DISKFILE);
		return -ENOENT;		//File not found
	}
	fseek(fd, 1*BLOCK_SIZE, SEEK_SET);	//Go to second block
	fread(FAT_block, BLOCK_SIZE, 1, fd);
	fclose(fd);
	return 0;
}

int write_FAT_block(struct cs1550_FAT_buf FAT_block){
	FILE *fd = fopen(DISKFILE, "r+b");			//Open disk for writing binary
	if(fd == NULL){
		printf("Error: Unable to open disk for FAT block write: %s\n", DISKFILE);
		return -ENOENT;		//File not found
	}
	fseek(fd, 1*BLOCK_SIZE, SEEK_SET);
	fwrite(&FAT_block, BLOCK_SIZE, 1, fd);
	fclose(fd);
	return 0;
}

int get_free_nStartBlock(struct cs1550_FAT_buf *FAT_block, int file_flag){
	get_FAT_block(FAT_block);
	int i = 2 //Skip blocks 0 and 1 (root and FAT)
;
        if ( file_flag ==1)
        {
          i = MAX_DIRS_IN_ROOT; // For files start blcoks after possible directories blocks
        }
	for(; i < MAX_NUM_BLOCKS; i++){	//Skip blocks 0 and 1 (root and FAT)
		if(FAT_block->nStartBlock[i] == UNUSED){
			FAT_block->nStartBlock[i] = USED;	//Set to used
			return i;			//Return block number
		}
	}

	
	return -1;	//Return -1 if unable to find any free blocks
}

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	int i = 0;
	int res = 0;

	strcpy(filename, "");
	strcpy(directory, "");
	strcpy(extension, "");

	if(DEBUG)printf("In getattr\n");

	memset(stbuf, 0, sizeof(struct stat));
	
   	struct cs1550_directory current_dir;

	if(strlen(path) != 1){ //Check if the input is anything but the root
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	}

	if(DEBUG)printf("---PATH--- [%s] directory: [%s], filename: [%s], extension: [%s]\n", path, directory, filename, extension);
	//Check if any names exceed the character limit
	if(strlen(directory) > MAX_FILENAME || strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION){
		if(DEBUG)printf("Input too long\n");
		return -ENAMETOOLONG;
	}

	//is path the root dir?
	//No more work required, return 0

	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	}
	strcpy(current_dir.dname, "");	//Initialize the current directory
	current_dir.nStartBlock = -1;
	if(get_root_block(&root_block) != 0){
		if(DEBUG)printf("Unable to get root block\n");
		return -ENOENT;
	}
	//Check for directory being passed
	for(i = 0; i < root_block.nDirectories; i++){
                if(DEBUG)printf("Checking Directory %s\n", root_block.directories[i].dname);
		if(strcmp(root_block.directories[i].dname, directory) == 0){
				if(DEBUG)printf("Directory %s found\n", root_block.directories[i].dname);
                                if(DEBUG)printf("nStartBlock %ld\n", root_block.directories[i].nStartBlock);
				break; //Found directory
		}
	}
		
	if( i >= root_block.nDirectories){
		printf("Directory not found\n");
		res = -ENOENT;
		return res;
	}
        
	current_dir = root_block.directories[i];

	//Check if name is subdirectory
	if(strcmp(filename, "") == 0){		//No file, only empty subdir 
		//Might want to return a structure with these fields
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		res = 0; //no error
		return res;
	}
	if(DEBUG)printf("Checking if regular file\n");

	//Check if name is a regular file
	//Process file by looking at current_dir.nStartBlock

	FILE *fd = fopen(DISKFILE, "r+b");
	fseek(fd, current_dir.nStartBlock*BLOCK_SIZE, SEEK_SET);	//Go to block where file in directory is stored

	//Subdirectory block
	cs1550_directory_entry subdir;
	i = fread(&subdir, BLOCK_SIZE, 1, fd);

	if (i < 0){
		printf("Error: Unable to read subdirectory block\n");
		res = -ENOENT;
		return res;
	}

	fclose(fd);

	//Using subdirectory block, process file info within the block

	
	struct cs1550_file_directory file_info;
	memset(&file_info, 0, sizeof(struct cs1550_file_directory));

	//Search for filename in block
	int j = 0;
	for(j = 0; j < subdir.nFiles; j++){
		if(strcmp(subdir.files[j].fname, filename) == 0){
			if(DEBUG)printf("File already exists, returning\n");
			//return -EEXIST;	//File already exists
			 stbuf->st_mode = S_IFREG | 0666;
		        stbuf->st_nlink = 1; //file links
		        stbuf->st_size = 0; //file size - make sure you replace with real size!
		        res = 0; // no error
			return res;
		        if(DEBUG)printf("In getattr, stbuf set to 666\n");
		}
	}
	//If file is not found
	if(j >= subdir.nFiles){
	//	int file_start_block = 0;
		printf("File not found\n");
		res = -ENOENT;
		return res;

	}

       /* //regular file, probably want to be read and write
        stbuf->st_mode = S_IFREG | 0666;
        stbuf->st_nlink = 1; //file links
//      stbuf->st_size = file_info.fsize; //file size - make sure you replace with real size!
        stbuf->st_size = 0; //file size - make sure you replace with real size!
        res = 0; // no error
	if(DEBUG)printf("In getattr, stbuf set to 666\n");*/

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

        strcpy(directory, "");
        strcpy(filename,"");
        strcpy(extension,"");

	int i = 0;
	
	if(DEBUG)printf("In readdir\n");

	if(DEBUG)printf("Path: %s\n",path);

	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	if(DEBUG)printf("Before filler\n");
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	if(DEBUG)printf("After filler\n");	

	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	if(DEBUG)printf("PATH: %s\n", path);

	if(DEBUG)printf("Directory %s, File %s, Extension %s\n", directory, filename, extension);
	if(strlen(directory) > MAX_FILENAME){
		if(DEBUG)printf("Directory name %s too long\n", directory);
		return -ENAMETOOLONG;
	}

	if(strlen(filename) > MAX_FILENAME){
		if(DEBUG)printf("File name %s too long\n", filename);
		return -ENAMETOOLONG;
	}

	if(strlen(extension) > MAX_EXTENSION){
		if(DEBUG)printf("Extension name %s too long\n", extension);
		return -ENAMETOOLONG;
	}

	//If root, then only subdirectories should exist, no files
	if (strcmp(path, "/") == 0){
		//Check if filename or extension, return error

		if(strlen(filename) > 0 || strlen(extension) > 0){
			printf("File cannot exist in root\n");
			return -EEXIST;
		}

	struct cs1550_root_directory root_block;
	//Open file and get root_block
	get_root_block(&root_block);
	if(DEBUG)printf("Number of directories = %d\n", root_block.nDirectories);

	printf("Listing directories\n");
	for(i = 0; i < root_block.nDirectories; i++){
			if(DEBUG)printf("Directory %s found\n", root_block.directories[i].dname);
			filler(buf, root_block.directories[i].dname, NULL, 0);	//List directory in buffer
			
	}
	return 0;
	}	//End of if
	else{			//In subdirectory, list the files in subdirectory
		FILE *fd;
		char file_buf[MAX_FILENAME + 1];
 	        struct cs1550_root_directory root_block;
        	//Open file and get root_block
        	get_root_block(&root_block);

	        for(i = 0; i < root_block.nDirectories; i++){
			if(strcmp(root_block.directories[i].dname, directory) == 0){
	                        if(DEBUG)printf("Directory %s found, start_block = %ld\n", root_block.directories[i].dname, root_block.directories[i].nStartBlock);
				break;
			}

        	}

		//Directory not found
		if(i == root_block.nDirectories){
			printf("Directory %s not found\n", directory);
			return -ENOENT;
		}

		if (DEBUG) printf("nStartblock of subdirectory=%ld \n", root_block.directories[i].nStartBlock);
		fd = fopen(DISKFILE, "r+b"); //Open for reading
		fseek(fd, root_block.directories[i].nStartBlock*BLOCK_SIZE,SEEK_SET); //Move to offset block
		cs1550_directory_entry file_listing;	//Store file info here

		memset(&file_listing, 0, sizeof(file_listing));
		memset(file_buf, 0, sizeof(file_buf));
		if(DEBUG)printf("Subdir nStartBlock = %ld\n", root_block.directories[i].nStartBlock);

		fread(&file_listing, BLOCK_SIZE, 1, fd);//read from that block
		if(DEBUG)printf("Listing files\n");

		int j = 0;

		if(DEBUG)printf("file_listing.nFiles = %d\n", file_listing.nFiles);
		for(j = 0; j < file_listing.nFiles; j++){ //Iterate over the non-empty filenames in this directory and print them to the user using filler()
			if(DEBUG)printf("Fname=<%s> exten=<%s>\n",file_listing.files[j].fname, file_listing.files[j].fext);
			strcpy(file_buf, file_listing.files[j].fname);	//Copied file name
			//Check for extension
			if(strcmp(file_listing.files[j].fext, "") != 0){	//Found extension
				strcat(file_buf, ".");	//Concatinate extension
				strcat(file_buf, file_listing.files[j].fext);
			}
			if(DEBUG)printf("Call filler for listing files\n");
			filler(buf, file_buf, NULL, 0);

		}
		fclose(fd);

	}
	/*
	//add the user stuff (subdirs or files)
	//the +1 skips the leading '/' on the filenames
	//filler(buf, newpath + 1, NULL, 0);
	*/
	return 0;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) path;
	(void) mode;

	int res = 0;

	if(DEBUG)printf("In mkdir\n");


	strcpy(filename, "");
	strcpy(directory, "");
	strcpy(extension, "");

	if(strlen(path) > 1){
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	}

	if(DEBUG)printf("Path: %s\n", path);

	if(strcmp(directory, "") == 0){
		printf("Invalid directory name\n");
		res = -ENOENT;
		return res;
	}

	get_root_block(&root_block);

	if(root_block.nDirectories >= MAX_DIRS_IN_ROOT){
		printf("Maximum number of directories reached\n");
		res = -ENOENT;
		return res;
	}

	int i = 0;

	if(DEBUG)printf("In mkdir, trying to create directory <%s>\n", directory); 
	for(i = 0; i < MAX_DIRS_IN_ROOT; i++){
		if(strcmp(root_block.directories[i].dname, "") != 0 && strcmp(root_block.directories[i].dname, directory) == 0){
			if(DEBUG)printf("Directory %s already exists\n", directory);
			res = -EEXIST;
			return res;
		}
	}

	//Write directory
	strcpy(root_block.directories[root_block.nDirectories].dname, directory);
	//Need to get nStarBlock from FAT table
	i = get_free_nStartBlock(&FAT_buf, 0);
	if(i > 0){
		root_block.directories[root_block.nDirectories].nStartBlock = i;
	}
	else{
		printf("Unable to find free block\n");
		res = -ENOENT;
		return res;
	}
	root_block.nDirectories++;

	if(DEBUG)printf("************In mkdir, nStartBlock = %d\n", i);


	write_FAT_block(FAT_buf);

	write_root_block(root_block);

	if(DEBUG)printf("Directory %s created with nStartBlock = %ld\n", directory, root_block.directories[root_block.nDirectories].nStartBlock);

	return res;
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

	strcpy(filename, "");
	strcpy(directory, "");
	strcpy(extension, "");

	int res;
	int i = 0;

	if(DEBUG)printf("In mknod\n");

	if(strlen(path) > 1){
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	}

	if(DEBUG)printf("Mknod Path: %s\n", path);
	if(DEBUG)printf("Path: directory [%s] file[%s] ext [%s]\n", directory, filename, extension);

	if(strcmp(directory, "") == 0){
		printf("Directory not specified (cannot create file in root dir)\n");
		return -EPERM;
	}

	if(strcmp(filename, "") == 0){
		printf("Invalid file name, file name is empty %s\n", path);
		res = -EPERM;
		return res;
	}

	//Check file name length
	if(strlen(filename) > MAX_FILENAME){
		if(DEBUG)printf("Mknod file too long\n");
		return -ENAMETOOLONG;
	}

	if(strcmp(extension, "") != 0){
		//File extension exists
		if(strlen(extension) > MAX_EXTENSION){
			if(DEBUG)printf("Mknod extension too long\n");
			return -ENAMETOOLONG;
		}
	}

	get_root_block(&root_block);

        // Get directory nStartBlock
	for(i = 0; i < root_block.nDirectories; i++){
		if(strcmp(root_block.directories[i].dname, "") != 0 && strcmp(root_block.directories[i].dname, directory) == 0){
			if(DEBUG)printf("mknod directory %s found\n", directory);
			break;	//Directory found
		}
	}

	if(i == root_block.nDirectories){
		//No directories by name found
		printf("No such %s directory found\n", directory);
		res = -ENOENT;
		return res;
	}

        // found directory name, using nStartBlock, go to that block

	FILE *fd = fopen(DISKFILE, "r+b");
	fseek(fd, root_block.directories[i].nStartBlock*BLOCK_SIZE, SEEK_SET);	//Go to the nStartBlock

	//Subdirectory block
	cs1550_directory_entry subdir;
	i = fread(&subdir, BLOCK_SIZE, 1, fd);

	if (i < 0){
		printf("Error: Unable to read subdirectory block\n");
		res = -ENOENT;
		return res;
	}

	fclose(fd);

	//Check if file already exists in subdir
	for(i = 0; i < subdir.nFiles; i++){
		if(strcmp(subdir.files[i].fname, filename) == 0 && 
		   strcmp(subdir.files[i].fext, extension) == 0){
			if(DEBUG)printf("File already exists\n");
			res = -EEXIST;
			return res;
		}
	}

	//If file does not exist
	if(DEBUG)printf("mknod file %s does not exist\n", filename);
	strcpy(subdir.files[i].fname, filename);
	strcpy(subdir.files[i].fext, extension);
	
	int free_start_block;
	free_start_block = get_free_nStartBlock(&FAT_buf, 1);	//Get free starting block for file

	if(free_start_block == -1){
		//There are no free blocks
		printf("No free blocks available\n");
		return -ENOENT;
	}
 
	subdir.files[i].nStartBlock = free_start_block;
	subdir.files[i].fsize = 0;
	subdir.nFiles++;	//Increment the number of files

	if(DEBUG)printf("mknod free_Start_block %d\n", free_start_block);

	fd = fopen(DISKFILE, "r+b");
	if(DEBUG)printf("Subdir Start Block %ld\n", root_block.directories[i].nStartBlock);
	fseek(fd, root_block.directories[i].nStartBlock*BLOCK_SIZE, SEEK_SET);	//Go to the nStartBlock

	fwrite(&subdir, BLOCK_SIZE, 1, fd);	//Write subdirectory block at location
	fclose(fd);

	if(DEBUG)printf("mknod write FAT block\n");
	write_FAT_block(FAT_buf);		//Write FAT block back with updated nStartBlock

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

	int res;
	int i = 0;

	if(DEBUG)printf("In read\n");

	strcpy(filename, "");
	strcpy(directory, "");
	strcpy(extension, "");

	if(strlen(path) > 1){
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	}

	if(DEBUG)printf("Path: %s\n", path);

	if(strcmp(directory, "") == 0){
		printf("Directory cannot be empty\n");
		res = -EPERM;
		return res;
	}
	

	if(DEBUG)printf("filename %s, extension %s\n", filename, extension);
	if(strcmp(filename, "") == 0){
		printf("Invalid file name, file name is empty %s\n", path);
		res = -EPERM;
		return res;
	}

	//Check file name length
	if(strlen(filename) > MAX_FILENAME){
		return -ENAMETOOLONG;
	}

	if(strcmp(extension, "") != 0){
		//File extension exists
		if(strlen(extension) > MAX_EXTENSION){
			return -ENAMETOOLONG;
		}
	}

	get_root_block(&root_block);		//Start from the beginning

        // Get directory nStartBlock
	for(i = 0; i < root_block.nDirectories; i++){
		if(strcmp(root_block.directories[i].dname, "") != 0 && strcmp(root_block.directories[i].dname, directory) == 0){
			break;	//Directory found
		}
	}

	if(i == root_block.nDirectories){
		//No directories by name found
		printf("No such %s directory found\n", directory);
		res = -ENOENT;
		return res;
	}
	

    // found directory name, using nStartBlock, go to that block

	FILE *fd = fopen(DISKFILE, "r+b");
	fseek(fd, root_block.directories[i].nStartBlock*BLOCK_SIZE, SEEK_SET);	//Go to the nStartBlock

	if(DEBUG)printf("Read Subdirectory nStartBlock %ld\n", root_block.directories[i].nStartBlock);
	//Subdirectory block
	cs1550_directory_entry subdir;
	i = fread(&subdir, BLOCK_SIZE, 1, fd);

	if (i < 0){
		printf("Error: Unable to read subdirectory block\n");
		res = -ENOENT;
		return res;
	}

	fclose(fd);

	//Check if file already exists in subdir
	for(i = 0; i < subdir.nFiles; i++){
		if(DEBUG)printf("Read filename %s %s\n", subdir.files[i].fname, subdir.files[i].fext);
		if(strcmp(subdir.files[i].fname, filename) == 0 && 
		   strcmp(subdir.files[i].fext, extension) == 0){
			//printf("File already exists\n");
			res = 0;
			break;
		}
	}

	if(i == subdir.nFiles){
		printf("Read File %s not found\n", filename);
		return -ENOENT;
	}
	
	//check to make sure path exists
	//check that size is > 0
	
	if(DEBUG)printf("-----------Size is %ld\n", size);

	if(size <= 0){
		printf("Error: Size cannot be 0\n");
		return -ENOENT;
	}
	//check that offset is <= to the file size

	if(offset > subdir.files[i].fsize*BLOCK_SIZE){
		printf("Error: offset out of bounds\n");
		res = -EFBIG;
		return res;
	}

	//Need to get to block in offset range
	int offset_block = offset / BLOCK_SIZE;

    //Jump to offset block for reading

	//???? Remove later
	//char output[10000];		//Will hold buf contents
	char output[size];


	fd = fopen(DISKFILE, "r+b");
//????	fseek(fd, subdir.files[i].nStartBlock*BLOCK_SIZE + offset_block, SEEK_SET);    //Go to location of $
    	fseek(fd, ((subdir.files[i].nStartBlock+1)*BLOCK_SIZE) + offset_block, SEEK_SET);    //Go to location of $

	
	//Read blocks until EOF marker in FAT 

	if(DEBUG)printf(">>>>>>offset : %ld offset_block %d\n", offset, offset_block);

	if(DEBUG)printf("Read File nStartBlock %ld\n", subdir.files[i].nStartBlock);
	
	int bytes = fread(output, size, 1, fd);

	if(DEBUG)printf("In read, output = %s	bytes %d	size %ld\n", output, bytes, size);
	if(DEBUG)printf("File nStartBlock offset %ld\n", subdir.files[i].nStartBlock);
	fclose(fd);
	
	memcpy(buf, output, strlen(output));

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//read in data
	//set size and return, or error

	//size = 0;
	size = strlen(buf);

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

	int res;
	int i = 0;

	if(DEBUG)printf("In write\n");

	cs1550_disk_block data_block[1+ offset/BLOCK_SIZE+ strlen(buf)/BLOCK_SIZE];	//For writing block
	memset(data_block, 0, sizeof(data_block));

	int bytes_to_write = strlen(buf);	//Number of bytes needed to write


	strcpy(filename, "");
	strcpy(directory, "");
	strcpy(extension, "");

	if(strlen(path) > 1){
		sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	}

	if(DEBUG)printf("Path: %s\n", path);

	if(strcmp(directory, "") == 0){
		printf("Directory cannot be empty\n");
		res = -EPERM;
		return res;
	}

	if(strcmp(filename, "") == 0){
		printf("Invalid file name, file name is empty %s\n", path);
		res = -EPERM;
		return res;
	}

	//Check file name length
	if(strlen(filename) > MAX_FILENAME){
		return -ENAMETOOLONG;
	}

	if(strcmp(extension, "") != 0){
		//File extension exists
		if(strlen(extension) > MAX_EXTENSION){
			return -ENAMETOOLONG;
		}
	}

	get_root_block(&root_block);		//Start from the beginning

    // Get directory nStartBlock
	for(i = 0; i < root_block.nDirectories; i++){
		if(strcmp(root_block.directories[i].dname, "") != 0 && strcmp(root_block.directories[i].dname, directory) == 0){
			break;	//Directory found
		}
	}

	if(i == root_block.nDirectories ){
		//No directories by name found
		printf("No such directory found\n");
		res = -ENOENT;
		return res;
	}

        // found directory name, using nStartBlock, go to that block

	FILE *fd = fopen(DISKFILE, "r+b");
	fseek(fd, root_block.directories[i].nStartBlock*BLOCK_SIZE, SEEK_SET);	//Go to the nStartBlock

	//Subdirectory block
	cs1550_directory_entry subdir;
	i = fread(&subdir, BLOCK_SIZE, 1, fd);

	if (i < 0){
		printf("Error: Unable to read subdirectory block\n");
		res = -ENOENT;
		return res;
	}

	fclose(fd);

	//Check if file already exists in subdir
	for(i = 0; i < subdir.nFiles; i++){
		if(strcmp(subdir.files[i].fname, filename) == 0 && 
		   strcmp(subdir.files[i].fext, extension) == 0){
			if(DEBUG) printf("File already exists\n");
			res = 0;
			break;
		}
		//???? check if not exists
	}
	
	//???? check if not exists
	if( i== subdir.nFiles ){
		if(DEBUG) printf("write(), File %s.%s does not exist\n", filename, extension);
		res = -ENOENT;
		return res;	
	}
	
	//check to make sure path exists
	//check that size is > 0

	if(size <= 0){
		printf("Error: Size cannot be 0\n");
		return -ENOENT;
	}
	//check that offset is <= to the file size

	if(DEBUG)printf("Offset: %ld	File size: %ld\n", offset, subdir.files[i].fsize);
	if(offset > subdir.files[i].fsize*BLOCK_SIZE){
		printf("Error: offset out of bounds\n");
		res = -EFBIG;
		return res;
	}

	//Need to get to block in offset range
	int offset_block = offset / BLOCK_SIZE;

    //Jump to offset block for writing

	fd = fopen(DISKFILE, "r+b");
	fseek(fd, (subdir.files[i].nStartBlock*BLOCK_SIZE) + offset_block, SEEK_SET);    //Go to location of file
	//Read block
	fread(&data_block, BLOCK_SIZE, 1, fd);

	//Copy data starting at offset and until BLOCK_SIZE
	//Get starting point from offset_block

	if(DEBUG)printf("Write() file nStartBlock %ld\n", subdir.files[i].nStartBlock);

	//Copy from offset start to end of data portion
	if(DEBUG)printf("Write() Data to be written: %s	bytes to write %d\n", buf,  bytes_to_write);
 	memcpy(&data_block[0].data[offset%BLOCK_SIZE], buf, bytes_to_write);
	
	int bytes = fwrite(&data_block[0], bytes_to_write, 1, fd);

	if(DEBUG)printf("write(), Bytes written %d, %s\n", bytes,&data_block[0].data[0] );

	fclose(fd);

	for(i = 0; i < strlen(buf)%BLOCK_SIZE; i++){		//Get and reserve as many blocks needed for file write/append
		get_free_nStartBlock(&FAT_buf, 1);
		write_FAT_block(FAT_buf);
	}
   	//Write EOF marker
	i=get_free_nStartBlock(&FAT_buf, 1);
        FAT_buf.nStartBlock[i]= EOF;
	write_FAT_block(FAT_buf);

	//write data
	//set size (should be same as input) and return, or error
    //???? 
	size = bytes_to_write;
	
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
