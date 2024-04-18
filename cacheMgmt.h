#include "yfs.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <comp421/yalnix.h>
#include <comp421/hardware.h>
#include <comp421/filesystem.h>
#include <comp421/iolib.h>

struct blockMetadata {
    int num;
    int isDirty;
    struct blockMetadata* prev;
    struct blockMetadata* next;
    char data[BLOCKSIZE];
}

struct blockEntry {
    int cacheKey;
    struct blockMetadata* metadata;
}

struct inodeMetadata {
    int num;
    int isDirty;
    struct inodeMetadata* prev;
    struct inodeMetadata* next;
    struct inode* value;
}

struct inodeEntry {
    int cacheKey;
    struct inodeMetadata* metadata;
}

void startCache();
int syncDiskCache();
struct blockMetadata* readBlockFromDisk(int num);
struct inodeMetadata* readInodeFromDisk(int num);

