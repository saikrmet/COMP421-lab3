#include "cacheMgmt.h"
#include <comp421/yalnix.h>
#include <comp421/filesystem.h>

#include <comp421/hardware.h>
#include <comp421/iolib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>



// helpers 
int getDir(char* file, int inum);
int removeFile(int fi, int di);
int getFB();
int resizeFile(struct inodeMetadata* inodeData, int s);
char* getBlockInputPlace(struct inodeMetadata* id, int dirty_temp, int place);
int createDir(struct dir_entry de, short di);
int getPI(short d, char* pn);
char* getPath(char* pn);
int newDirectory(char* fn, int new_type, short pi);
void createFreeInodeAndBlock();
struct dir_entry initEntry(char* file, short incode);

// handlers 
int create_handler(char *pathname, short directory);
int read_handler(int fd, void *buf, int size, short curr);
int write_handler(int fd, void *buf, int size, short curr);
int seek_handler(short curr);
int link_handler(char *oldName, char *newName, short directory);
int unlink_handler(char *pathname, short directory);
int symLink_handler(char *oldName, char *newName, short directory);
int readLink_handler(char *pathname, char *buf, int len, short directory);
int mkDir_handler(char *pathname, short directory);
int rmDir_handler(char *pathname, short directory);
int chDir_handler(char *pathname, short directory);
int stat_handler(char *pathname, struct Stat *statbuf, short directory);
int sync_handler(void);
int shutdown_handler(void);
int open_handler(char *pn, int pi);

// final message handler 
int message_handle(char* msg, int pid);

int inode_size;
int block_size;

short* inodes_free;
short* blocks_free;


// PROCEDURE CALL REQUESTS>>>>>>

int create_handler(char *pathname, short directory) {
    // create the directory 
    return newDirectory(getPath(pathname), INODE_REGULAR, getPI(directory, pathname));
}



// changed parameters here 
int read_handler(int fd, void *buf, int size, short curr) {
    // create the data
    struct inodeMetadata* d = readInodeFromDisk(curr);
    if(d->num == -1) return ERROR;
    // loop through to set the placement correctly and return it
    int placement = 0;
	struct inode* fi = d->value;
    int new_size = fi->size;
    while(placement < size && fd + placement < new_size) {
        char* blck = getBlockInputPlace(d, 0, fd + placement);
		int remainingSpace = BLOCKSIZE - ((fd + placement) % BLOCKSIZE);
		if(size - placement < remainingSpace) remainingSpace = size - placement;
		if(new_size - (fd + placement) < remainingSpace) remainingSpace = new_size - fd - placement;
		memcpy(buf + placement, blck, remainingSpace);
        // update placement with new space 
		placement += remainingSpace;
    }
    return placement;
}

// changed parameters fd=parameter
int write_handler(int fd, void *buf, int size, short curr) {
    struct inodeMetadata* d = readInodeFromDisk(curr);
    d->isDirty = 1;
    if(d->num == -1 || resizeFile(d, fd + size) == ERROR) return ERROR;
    int placement = 0;
    // loop through again to set placement similar to read_handler
    while(placement < size && fd + placement < d->value->size) {
		char* blck = getBlockInputPlace(d, 1, fd + placement);
		int remainingSpace = BLOCKSIZE - (fd + placement) % BLOCKSIZE;
		if(size - placement < remainingSpace) remainingSpace = size - placement;
        // copy the memory then add to placement. 
		memcpy(blck, placement + buf, remainingSpace);
		placement += remainingSpace;
    }
    return placement;
}


int seek_handler(short curr) {
    struct inodeMetadata* in = readInodeFromDisk(curr);
    if(in->num == -1) {
		printf("seek handler error\n");
		return ERROR;
    }
    return in->value->size;
}

int link_handler(char *oldName, char *newName, short directory) {
    short file_open = open_handler(oldName, directory);
    struct inodeMetadata* temp = readInodeFromDisk(file_open);
    if(temp->value->type == INODE_DIRECTORY || temp->num == -1) {
		printf("link handler erroring\n");
		return ERROR;
    }

    char* path = getPath(newName);
    short pi = getPI(directory, newName);
    struct dir_entry temp_entry = initEntry(path, file_open);
    createDir(temp_entry, pi);
    // update values after getting directory 
    temp->isDirty = 1;
    temp->value->nlink++;
    return 0;
}


int unlink_handler(char *pathname, short directory) {
    short d = open_handler(pathname, directory);
    struct inodeMetadata* temp = readInodeFromDisk(d);
    if(temp->value->type == INODE_DIRECTORY) {
		printf("bad link (unlink)\n");
		return ERROR;
    }
    if (temp->num == -1)
    {
        printf("bad link (unlink handler) \n");
        return ERROR;
    }
    short pi = getPI(directory, pathname);
    removeFile(d, pi);
    temp->value->nlink-=1;
    temp->isDirty = 1;
    return 0;
}


int symLink_handler(char *oldName, char *newName, short directory) {
    short inum = newDirectory(getPath(newName), getPI(directory, newName), INODE_SYMLINK);
    if(inum == ERROR){
		return ERROR;
	}
    return write_handler(0,(void*)oldName, strlen(oldName), inum);
}

int readLink_handler(char *pathname, char *buf, int len, short directory) {
    return read_handler(0, buf, len, getDir(getPath(pathname), getPI(directory,pathname)));
}


int mkDir_handler(char *pathname, short directory) {

    short pi = getPI(directory,pathname);
    char* pn = getPath(pathname);
    
    short file_temp = newDirectory(pn, INODE_DIRECTORY, pi);
    if(file_temp == ERROR){
        TracePrintf(1, "mkdir handler error ");
		return ERROR;
	}
    createDir(initEntry(".",file_temp), file_temp);
    createDir(initEntry( "..", pi), file_temp);
    return 0;
}

int rmDir_handler(char *pathname, short directory) {
    int length = strcmp(pathname, "/") * strcmp(pathname, "..") * strcmp(pathname, ".") ;
    if(length == 0) {
		printf("pathname error \n");
		return ERROR;
    }
    short open_i = open_handler(pathname, directory);
    struct inodeMetadata* d = readInodeFromDisk(open_i);
    if (d->value->type != INODE_DIRECTORY) {
        printf("bad directory \n");
        return ERROR;
    }
    if(d->num == -1) {
		printf("bad directory\n");
		return ERROR;
    }

    int c;
    for(c = 2; c < (int)(d->value->size / sizeof(struct dir_entry)); c++) {
        // create emptry directory entry to read
		struct dir_entry de;
		read_handler(sizeof(de)*c,(void*)&de, sizeof(de), open_i);
		if(de.inum != 0) {
			printf("directory is empty \n");
			return ERROR;
		}
    }
    inodes_free[open_i] = 0;  
    removeFile(getPI(directory, pathname), open_i);
    d->value->type = INODE_FREE;
    d->isDirty = 1;
    return 0;
}


int chDir_handler(char *pathname, short directory) {
    short d = open_handler(pathname, directory);
    if(readInodeFromDisk(d)->value->type != INODE_DIRECTORY) {
		printf("bad directory\n");
		return ERROR;
    }
    return d;
}


int stat_handler(char *pathname, struct Stat* statbuf, short directory) {
    short inum = open_handler(pathname, directory);
    struct inodeMetadata* id = readInodeFromDisk(inum);
    if(id->num == -1) {
		printf("bad name for stat handler\n");
		return ERROR;
    }
    statbuf->nlink = id->value->nlink;
    statbuf->type = id->value->type;
    statbuf->size = id->value->size;
    // don't forget the inum statbuf update.    
    statbuf->inum = id->num;
    return 0;
}

int sync_handler(void) {
    return syncDiskCache();
}

int shutdown_handler(void) {
    syncDiskCache(); 
    return 0;
}

int open_handler(char *pn, int pi) {
	TracePrintf(1, "open_handler\n");
    if(pn == NULL) return pi;
    int curr;
    char name[1+DIRNAMELEN];
    memset(name,'\0',DIRNAMELEN + 1);
    curr = (pn[0] != '/') ? pi : ROOTINODE;
    while(*pn == '/') {
		pn+=1;
    }
    int links = 0;
    
    while(strlen(pn) != 0) {
		memset(name,'\0',DIRNAMELEN + 1);
        int s = strlen(pn);
		while(s > 0 && *pn == '/') {
			pn+=1;
			s-=1;
		}
        int counter = 0;
		while(s > 0 && *pn != '/') {
			name[counter] = *pn;
            // update indices 
			pn++;
            counter++;
			s--;
		}


		struct inodeMetadata* data;
		int currInum = getDir(name, curr);
		if (currInum < 1) {
			printf("bad getDir name\n");
			return ERROR;
		}

		data = readInodeFromDisk(currInum);
        // retrieve the type 
		if (data->value->type == INODE_SYMLINK) {
			if(links < MAXSYMLINKS) {
				int inodeSize = data->value->size;
				char* callocPathName = (char*)calloc(sizeof(char) * inodeSize + s + 1, 1);
				struct blockMetadata* block = readBlockFromDisk(data->value->direct[0]);
				memcpy(callocPathName, block->data, inodeSize);

				if(s > 0) {
                    // add the / and pathname 
					strcat(callocPathName, "/");
					strcat(callocPathName, pn);
				}
				pn = callocPathName;
				links++;
				if(callocPathName[0] == '/') {
					curr = ROOTINODE;
					continue;
				}
			} else{
                printf("too many links \n");
				return ERROR;
			}
		}
        // go next outside of the if statement
		curr = currInum;
    }
	TracePrintf(1, "successfully created inode number\n");
    return curr;
}




// HELPER FUNCTIONS FOR PROCEDURE CALLS >>>>>>>




struct dir_entry initEntry(char* file, short incode) {
    TracePrintf(1, "initEntry\n");
    struct dir_entry de;
    de.inum = incode;
    int f = strlen(file);
    if (f > DIRNAMELEN) f = DIRNAMELEN;
    memset(&de.name, '\0', DIRNAMELEN);
    memcpy(&de.name, file, f);
    return de;
}

int getDir(char* file, int inum) {
    TracePrintf(1, "getDir\n");
    struct inodeMetadata* data = readInodeFromDisk(inum);
    if(data->num == ERROR) {
        printf("error getting directory\n");
        return ERROR;
    }
    struct dir_entry de;
    //# divide by the size of the dir_entry 
    int s = data->value->size / sizeof(struct dir_entry);
    int i;
    for(i = 0; i < s; i++) {
        if(read_handler(i * sizeof(struct dir_entry), (void*)&de, sizeof(de), inum) == ERROR) 
        {
            printf("error getting directory\n");
            return ERROR;
        }
        if(strncmp(file, de.name, DIRNAMELEN) == 0) return de.inum;
    }
    return 0;
}


//takes in the directory inum and the file inum and deletes the file
// in the directory 
int removeFile(int fi, int di) {
    TracePrintf(1, "removeFile\n");
    struct inodeMetadata* data = readInodeFromDisk(di);
    // always error check. 
    if(data->num == ERROR) {
        printf("error removing file\n");
		return ERROR;
    }
    int s = data->value->size / sizeof(struct dir_entry);
    struct dir_entry de;
    int i;
    for(i = 2; i < s; i++) {
		if(read_handler(i * sizeof(struct dir_entry), (void*)&de, sizeof(de), di) == ERROR) {
            printf("error removing file\n");
			return ERROR;
		}
		if(fi == de.inum) {
			char* empty = "\0";
			struct dir_entry de_temp = initEntry(empty, 0);
			write_handler(i * sizeof(struct dir_entry), (void*)&de_temp, sizeof(de_temp), di);
			return 0;
		}
    }
	printf("error removing file\n");
    return ERROR;
}


int getFB() {
	TracePrintf(1, "");
    int i;
    // check 0 blocks first
    for (i=0; i < block_size; ++i) {
		if(blocks_free[i] == 0) {
			blocks_free[i] = 1;
			return i;
		}
    }
    syncDiskCache();
    createFreeInodeAndBlock();

    for (i = 0; i < block_size; ++i) {
		if(blocks_free[i] == 0) {
			TracePrintf(1, "got block\n");
			return i;
		}
    }
    printf("no free block available\n");
    return ERROR;
}


int resizeFile(struct inodeMetadata* inodeData, int s) {
	TracePrintf(1, "\n");
    struct inode* fi = inodeData->value;
    inodeData->isDirty = 1;
    if(s < fi->size) {
		return 0;
    }
    int size_c = ((fi->size + (BLOCKSIZE-1)) / BLOCKSIZE) * BLOCKSIZE;
    if(size_c < BLOCKSIZE * NUM_DIRECT) {
        while(s > size_c && size_c < BLOCKSIZE * NUM_DIRECT ) {
			int fb = getFB();
            // we are first checking if it errors here for the free block
            // get the free block and set the direct field block after
            // memsetting. 
			if(fb == ERROR) {
                TracePrintf(1, "error on free block when increasing file size \n");
				return ERROR;
			}
			struct blockMetadata* inodeData = readBlockFromDisk(fb);
			inodeData->isDirty = 1;
			memset(inodeData->data, '\0', BLOCKSIZE);
			fi->direct[size_c / BLOCKSIZE] = fb;
			size_c += BLOCKSIZE;
        }
    }
    if(size_c == BLOCKSIZE * NUM_DIRECT && s > size_c) {
        int fb_temp = getFB();
        // forget to error check
		if(fb_temp == ERROR) return ERROR;
		fi->indirect = fb_temp;
    }
    if(size_c < s) {
        // last edge case for when the size of the current dir is smaller and not equal to blocksize
		int extraBlockNum = fi->indirect;

		struct blockMetadata* bl = readBlockFromDisk(extraBlockNum);
		bl->isDirty = 1;
		int* bl_array = (int*)(bl->data);
		while(size_c < (int)(BLOCKSIZE * (NUM_DIRECT + BLOCKSIZE / sizeof(int))) && size_c < s) {
			int fb = getFB();
			if(fb == ERROR)return ERROR;
            // set array to free block 
			bl_array[size_c / BLOCKSIZE - NUM_DIRECT] = fb;
			size_c += BLOCKSIZE;
		}
    }
    // resize here 
    fi->size = s;
    return 0;
}

char* getBlockInputPlace(struct inodeMetadata* id, int dirty_temp, int place) {
	TracePrintf(1, "getBlockInputPlace\n");
    struct inode* fi = id->value;
    // bad input, reading size bigger than file size 
    if(place > fi->size) return NULL;
    int pos = place / BLOCKSIZE;
    if(pos < NUM_DIRECT) {

        struct blockMetadata* bd = readBlockFromDisk(fi->direct[pos]);
        if(dirty_temp == 1 && bd->isDirty != 1) bd->isDirty = dirty_temp;
        return bd->data + place % BLOCKSIZE;
    }
    struct blockMetadata* bd = readBlockFromDisk(fi->indirect);
    int readBlockNum = ((int*)(bd->data))[pos - NUM_DIRECT];
    struct blockMetadata* targetInfo = readBlockFromDisk(readBlockNum);
    // set the dirty var back
    targetInfo->isDirty = dirty_temp;
    return ((char*)(targetInfo->data)) + place % BLOCKSIZE;
}



int createDir(struct dir_entry de, short di) {
	TracePrintf(1, "createDir\n");
    struct inodeMetadata* dd = readInodeFromDisk(di);
    
	struct dir_entry temp;
    dd->isDirty = 1;
    int s = dd->value->size;
    int idx = 0;
    while(idx < s) {

		read_handler(idx, &temp, sizeof(temp), di);
		if(temp.inum == 0) {
			int go = write_handler(idx, &de, sizeof(de), di);
			if(go == ERROR) {
                return ERROR;
                
			}
			else{
				// read disk inode then set the fields and return
				struct inodeMetadata* id = readInodeFromDisk(de.inum);

                id->isDirty = 1;
				id->value->nlink+=1;
				
				return go;
			}
		}
        // update idx after every loop
		idx += sizeof(temp);
    }
    int go = write_handler(idx, &de, sizeof(de), di);

    if(go == ERROR) return ERROR;

    struct inodeMetadata* id = readInodeFromDisk(de.inum);
    id->value->nlink+=1;
    id->isDirty = 1;
    return go;
}


int getPI(short d, char* pn) {
	TracePrintf(1, "getPI\n");
    int c;
    for(c = strlen(pn) - 1; c >= 0; c--) {
        // if / symbol just break out to update i
		if(pn[c] == '/') break;
    }
    char* createSpace = (char*)malloc(c + 1 + 1);
    memcpy(createSpace, pn, c + 1);
    createSpace[c + 1] = '\0';

    if(!(c + 1 == 1 && createSpace[0] == '/')) {
		createSpace[c + 1-1] = '\0';
    }
    short getPI = open_handler(createSpace, d);
    free(createSpace);
    if(getPI == ERROR) return ERROR;
    else return getPI;
}


char* getPath(char* pn) {
	TracePrintf(1, "getPath\n");
    int c;
    // loop through to check for / 
    for(c=strlen(pn) - 1; c>-1; c--) {
		if(pn[c] == '/') {
			return pn + c + 1;
		}
    }
    return pn;
}

int newDirectory(char* fn, int new_type, short pi) {
	TracePrintf(1, "newDirectory\n");
    short new_num = getDir(fn, pi);
    // if error and file exists already return error
    if(new_num == ERROR || new_num != 0) return ERROR;
    
    short c;
    for(c=0; c < inode_size; c++) {
		if(inodes_free[c] == 0) {
			new_num = c;
			break;
		}
    }
    if(new_num == inode_size) return ERROR;
    inodes_free[c] = 1;
    
    struct inodeMetadata* temp = readInodeFromDisk(new_num);
    temp->isDirty = 1;
    
    struct inode* fileInode = temp->value;

    struct dir_entry de = initEntry(fn, new_num);

    fileInode->size = 0;
    fileInode->type = new_type;
    fileInode->reuse++;
    fileInode->nlink = 0;

    if(createDir(de, pi) != ERROR) {
        return new_num;
        
    }
    else {
        fileInode->nlink = 0;
		inodes_free[new_num] = 0;
		fileInode->type = INODE_FREE;
		fileInode->reuse-=1;
		return ERROR;
    }
}

void createFreeInodeAndBlock() {
    TracePrintf(1, "createFreeInodeAndBlock\n");
    struct inodeMetadata* id = readInodeFromDisk(0);
    struct fs_header* fsh = (struct fs_header*)(id->value);

    inode_size = fsh->num_inodes;
    inode_size = fsh->num_blocks;

    // initialize the free inodes list and free block list
    inodes_free = (short*)malloc(inode_size * sizeof(short));

    blocks_free = (short*)malloc(block_size * sizeof(short));
     
    

    int c;
    for (c = 0; c < block_size; ++c) {
		blocks_free[c] = 0;
        if (c == 0) blocks_free[c] = 1;
    }
    for(c = 1; c < 1 + ((inode_size + 1) * INODESIZE) / BLOCKSIZE; c++) {
		blocks_free[c] = 1;
    }


    int x;
    for (x = 0; x < inode_size; ++x) {
		inodes_free[x] = 0;
        if (c == 0 || c == 1) inodes_free[c] = 1;
    }
    for(x = 1; x < inode_size + 1; x++) {
		struct inode* di = readInodeFromDisk(x)->value;
		
		if(di->type != INODE_FREE) {
			inodes_free[x] = 1;
			int counter = 0;
            int di_size = di->size;
			while(counter * BLOCKSIZE < di_size && NUM_DIRECT> counter) {
				blocks_free[di->direct[counter]] = 1;
				counter += 1;
			}
            // do one more check if the current counte ris less than the size 
			if(counter * BLOCKSIZE < di_size) {
                TracePrintf(1, "check if infinite looping \n");
				int* get_data = (int*)(readBlockFromDisk(di->indirect)->data);
				blocks_free[di->indirect] = 1;
				while(counter < (di_size + (BLOCKSIZE-1)) / BLOCKSIZE) {
					blocks_free[get_data[counter - NUM_DIRECT]] = 1;
					counter+=1;
				}

			}
		}
    }
}



int message_handle(char* msg, int pid) {
#pragma GCC diagnostic push 
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
    
    
    uint8_t uint8_temp = (uint8_t)(msg[0]);
    int f;
    // create a temp message to make sure we can still
    // use message for the shutdown.
    char* m = msg;

    switch(uint8_temp) {
	case 1:{
	    short directory;
        char* curr_pn; 
        int pn_size; 
        
        m += sizeof(uint8_temp);
        memcpy(&curr_pn, m, sizeof(curr_pn));
        m += sizeof(curr_pn);
        memcpy(&pn_size, m, sizeof(pn_size));
        m += sizeof(pn_size);
        memcpy(&directory, m, sizeof(directory));
        char* pn = (char*)calloc(1+pn_size, sizeof(char));
        CopyFrom(pid, pn, curr_pn, 1+pn_size);
        f = open_handler(pn, directory);
        free(pn);
	    break;
	}
	case 3:{
	    short directory;
        char* curr_pn; 
        int pn_size; 
        
        m += sizeof(uint8_temp);
        memcpy(&curr_pn, m, sizeof(curr_pn));
        m += sizeof(curr_pn);
        memcpy(&pn_size, m, sizeof(pn_size));
        m += sizeof(pn_size);
        memcpy(&directory, m, sizeof(directory));
        char* pn = (char*)calloc(1+pn_size, sizeof(char));
        CopyFrom(pid, pn, curr_pn, 1+pn_size);
        f = create_handler(pn, directory);
        free(pn);
        return f;
	    break;
	}
	case 4:{
        int fd;
        int curr_size; 
	    char* curr_buffer;
		short curr_id; 
		
        m += sizeof(uint8_temp);
	    memcpy(&curr_buffer, m, sizeof(curr_buffer));
        m += sizeof(curr_buffer);
	    memcpy(&curr_size, m, sizeof(curr_size));
        m += sizeof(curr_size);
	    memcpy(&curr_id, m, sizeof(curr_id));
        m += sizeof(curr_id);
	    memcpy(&fd, m, sizeof(fd));
	    char* temp_buf = calloc(1+curr_size, sizeof(char));
	    f = read_handler(fd, temp_buf, curr_size, curr_id);
        // copy to for read and copy from in write
	    CopyTo(pid, curr_buffer, temp_buf, curr_size + 1);
	    free(temp_buf);
	    break;
	}
	case 5:{
	    int fd;
        int curr_size; 
	    char* curr_buffer;
		short curr_id; 

	    m += sizeof(uint8_temp);
	    memcpy(&curr_buffer, m, sizeof(curr_buffer));
        m += sizeof(curr_buffer);
	    memcpy(&curr_size, m, sizeof(curr_size));
        m += sizeof(curr_size);
	    memcpy(&curr_id, m, sizeof(curr_id));
        m += sizeof(curr_id);
	    memcpy(&fd, m, sizeof(fd));
	    char* temp_buf = calloc(1+curr_size, sizeof(char));
	    CopyFrom(pid, temp_buf, curr_buffer, curr_size + 1);
	    f = write_handler(fd, temp_buf, curr_size, curr_id);
	    free(temp_buf);
	    break;
	}
	case 6:{
        m+= sizeof(uint8_temp); 
        short id;
	    memcpy(&id, m, sizeof(id));
	    f = seek_handler(id);
        // free maybe ??? check later 

	    break;
	}
	case 7:{
	    char* curr_oldName; 
		int curr_oldName_size; 
		char* curr_newName; 
		int curr_nnSize;
	    short directory;

        m += sizeof(uint8_temp);
	    memcpy(&curr_oldName, m, sizeof(curr_oldName));
        m += sizeof(curr_oldName);
	    memcpy(&curr_oldName_size, m, sizeof(curr_oldName_size));
        m += sizeof(curr_nnSize);
	    memcpy(&curr_newName, m, sizeof(curr_newName));
        m += sizeof(curr_newName);
	    memcpy(&curr_nnSize, m, sizeof(curr_nnSize));
        m += sizeof(curr_nnSize);
	    memcpy(&directory, m, sizeof(directory));

		char* nn = calloc(1+curr_nnSize, sizeof(char));
	    
        CopyFrom(pid, nn, curr_newName, 1+curr_nnSize);

        char* ona = calloc(1+curr_oldName_size, sizeof(char));
        // copy old and new 
	    CopyFrom(pid, ona, curr_oldName, 1+curr_oldName_size);
	    
	    f = link_handler(ona, nn, directory);
	    break;
	}
	case 8: {
        short directory;

	    char* curr_pn; 
		int pn_size; 
		

	    m += sizeof(uint8_temp);
	    memcpy(&curr_pn, m, sizeof(curr_pn));
        m += sizeof(curr_pn);
	    memcpy(&pn_size, m, sizeof(pn_size));
        m += sizeof(pn_size);
	    memcpy(&directory, m, sizeof(directory));
	    char* pn = (char*)calloc(1+pn_size, sizeof(char));
	    CopyFrom(pid, pn, curr_pn, 1+pn_size);
	    f = unlink_handler(pn, directory);
	    free(pn);
	    break;
	}
	case 9: {
        short directory;

	    char* curr_oldName; 
		int curr_oldName_size; 
		char* curr_newName; 
		int curr_nnSize;


	    m += sizeof(uint8_temp);
	    memcpy(&curr_oldName, m, sizeof(curr_oldName));
        m += sizeof(curr_oldName);
	    memcpy(&curr_oldName_size, m, sizeof(curr_oldName_size));
        m += sizeof(curr_nnSize);
	    memcpy(&curr_newName, m, sizeof(curr_newName));
        m += sizeof(curr_newName);
	    memcpy(&curr_nnSize, m, sizeof(curr_nnSize));
        m += sizeof(curr_nnSize);
	    memcpy(&directory, m, sizeof(directory));
	    char* ona = calloc(1+curr_oldName_size, sizeof(char));
        CopyFrom(pid, ona, curr_oldName, 1+curr_oldName_size);

	    char* nn = calloc(1+curr_nnSize, sizeof(char));
	    CopyFrom(pid, nn, curr_newName, 1+curr_nnSize);
	    f = symLink_handler(ona, nn, directory);
        // not sure about the free here idk if it changes anything 
        free(nn);
	    break;
	}
	case 10: {
        short directory;

	    char* curr_pn; 
		int pn_size; 
		char* curr_buffer; 
		int curr_len2; 
		
        m += sizeof(uint8_temp);
	    memcpy(&curr_pn, m, sizeof(curr_pn));
        m += sizeof(curr_pn);
	    memcpy(&pn_size, m, sizeof(pn_size));
        m += sizeof(pn_size);
	    memcpy(&curr_buffer, m, sizeof(curr_buffer));
        m += sizeof(curr_buffer);
	    memcpy(&curr_len2, m, sizeof(curr_len2));
        m += sizeof(curr_len2);
	    memcpy(&directory, m, sizeof(directory));
		char* temp_buf = calloc(1+curr_len2, sizeof(char));
	    char* pn = calloc(1+pn_size, sizeof(char));
	    CopyFrom(pid, pn, curr_pn, 1+pn_size);
	    f = readLink_handler(pn, temp_buf, curr_len2, directory);
	    CopyTo(pid, curr_buffer, temp_buf, 1+curr_len2);

        free(pn);
		free(temp_buf);
	    
	    break;
	}
	case 11: {
        short directory;
	    char* curr_pn; 
		int pn_size; 
		
        m += sizeof(uint8_temp);
	    memcpy(&curr_pn, m, sizeof(curr_pn));
        m += sizeof(curr_pn);
	    memcpy(&pn_size, m, sizeof(pn_size));
        m += sizeof(pn_size);
	    memcpy(&directory, m, sizeof(directory));
	    char* pn = (char*)calloc(1+pn_size, sizeof(char));
	    CopyFrom(pid, pn, curr_pn, 1+pn_size);
	    f = mkDir_handler(pn, directory);
	    free(pn);
	    break;
	}
	case 12:{
        short directory;
	    char* curr_pn; 
		int pn_size; 
		
	    m += sizeof(uint8_temp);
	    memcpy(&curr_pn, m, sizeof(curr_pn));
        m += sizeof(curr_pn);
	    memcpy(&pn_size, m, sizeof(pn_size));
        m += sizeof(pn_size);
	    memcpy(&directory, m, sizeof(directory));
	    char* pn = (char*)calloc(1+pn_size, sizeof(char));
	    CopyFrom(pid, pn, curr_pn, 1+pn_size);
	    f = rmDir_handler(pn, directory);
	    free(pn);
	    break;
	}
	case 13:{
        short directory;
	    char* curr_pn; 
		int pn_size; 
		
	    m += sizeof(uint8_temp);
	    memcpy(&curr_pn, m, sizeof(curr_pn));
        m += sizeof(curr_pn);
	    memcpy(&pn_size, m, sizeof(pn_size));
        m += sizeof(pn_size);
	    memcpy(&directory, m, sizeof(directory));

	    char* pn = (char*)calloc(1+pn_size, sizeof(char));
	    CopyFrom(pid, pn, curr_pn, 1+pn_size);
	    f = chDir_handler(pn, directory);
	    free(pn);
	    break;
	}
	case 14:{
        short directory;
	    char* curr_pn; 
		int pn_size; 
		struct Stat* sb; 
		
        m+=sizeof(uint8_temp);
	    memcpy(&curr_pn, m, sizeof(curr_pn));
        m += sizeof(curr_pn);
	    memcpy(&pn_size, m, sizeof(pn_size));
        m += sizeof(pn_size);
	    memcpy(&sb, m, sizeof(sb));
        m += sizeof(sb);
	    memcpy(&directory, m, sizeof(directory));

	    char* pn = (char*)calloc(1+pn_size, sizeof(char));
	    struct Stat* sb2 = (struct Stat*)calloc(1, sizeof(struct Stat));
	    CopyFrom(pid, pn, curr_pn, 1+pn_size);

	    f = stat_handler(pn, sb2, directory);
	    CopyTo(pid, sb, sb2, sizeof(struct Stat));
		
	    free(pn);
        free(sb2);

	    break;
	}
	case 15:{
	    f = sync_handler();
	    break;
	}
	case 16:{
	    f = shutdown_handler();
        // refernece msg here again 
	    if(f == 0) {
			int c = 0;
			for(c = 0; c < 32; c++) {
				msg[c] = '\0';
			}
			memcpy(msg, &f, sizeof(f));
			Reply(msg, pid);      
			printf("Terminating, lets gooo\n");
			Exit(0);
	    }
	    break;
	}
	default: {
	    f = ERROR;
	}

    }
    return f;
#pragma GCC diagnostic pop 
}




int main(int argc, char** argv) {
    startCache();
    createFreeInodeAndBlock();    
    Register(FILE_SERVER);
    printf("Creating YFS\n");


    if (argc > 1) {
        int pid = Fork();

        if (pid == 0) {
            Exec(argv[1], argv + 1);
            printf("exec fail\n");
            Halt();
        } else if (pid < 0) {
            // Fork failed
            printf("fork fail\n");
            Halt();
        }
    }
    
    while(1) {
        // go through messages 
		char msg[32];
		int pid = Receive(msg);
		if(pid == -1) {
			printf("Recieve() error!\n");
			Exit(0);
		}
		if(pid == 0) {
			TracePrintf(1, "Receive() Error!\n");
			Exit(0);
		}
		int res = message_handle(msg, pid);
		int c;
		for(c = 0; c < 32; c++) {
			msg[c] = '\0';
		}
		memcpy(msg, &res, sizeof(res));
		Reply(msg, pid);      
    }   
    return 0;
}
