#include "cacheMgmt.h"

#include <comp421/filesystem.h>

int currBlockNum;
struct blockEntry* blockHashTable[BLOCK_CACHESIZE]; 
struct blockMetadata* freeBlockMetadata;
struct blockEntry* freeBlockEntry;
struct blockMetadata* firstBlock;
struct blockMetadata* lastBlock;

int currInodeNum;
struct inodeEntry* inodeHashTable[INODE_CACHESIZE];
struct inodeMetadata* freeInodeMetadata; 
struct inodeEntry* freeInodeEntry;
struct inodeMetadata* firstInode;
struct inodeMetadata* lastInode;

void startCache() {
    TracePrintf(1, "startCache(): initializing variables\n");
    currBlockNum = 0;
    currInodeNum = 0;

    int i;
    for (i = 0; i < BLOCK_CACHESIZE; i++) {
        blockHashTable[i] = NULL;
    }

    for (i = 0; i < INODE_CACHESIZE; i++) {
        inodeHashTable[i] = NULL;
    }

    freeBlockMetadata = (struct blockMetadata*) malloc(sizeof(struct blockMetadata));
    freeBlockMetadata->num = -1;
    freeBlockEntry = (struct blockEntry*) malloc(sizeof(struct blockEntry));
    freeBlockEntry->cacheKey = -1;
    firstBlock = NULL;
    lastBlock = NULL;

    freeInodeMetadata = (struct inodeMetadata*) malloc(sizeof(struct inodeMetadata));
    freeInodeMetadata->num = -1;
    freeInodeEntry = (struct inodeEntry*) malloc(sizeof(struct inodeEntry));
    freeInodeEntry->cacheKey = -1;
    firstInode = NULL;
    lastInode = NULL;
}

struct blockEntry* fetchBlock(int cacheKey) {
    int currHash = cacheKey % BLOCK_CACHESIZE;
    int firstHash = currHash;

    while (blockHashTable[currHash] != NULL) {
        if (blockHashTable[currHash]->cacheKey != cacheKey) {
            currHash++;
            currHash %= BLOCK_CACHESIZE;

            if (currHash == firstHash) {
                TracePrintf(1, "fetchBlock(): key not found\n");
                break;
            }

        } else {
            return blockHashTable[currHash];
        }
    }
    return NULL;
}

void insertBlockIntoHashTable(struct blockMetadata* metadata, int cacheKey) {
    struct blockEntry* entry = (struct blockEntry*) malloc(sizeof(struct blockEntry));
    entry->cacheKey = cacheKey;
    entry->metadata = metadata;
    int currHash = cacheKey % BLOCK_CACHESIZE;

    while (blockHashTable[currHash] != NULL && blockHashTable[currHash]->cacheKey != -1) {
        currHash++;
        currHash %= BLOCK_CACHESIZE;
    }
    blockHashTable[currHash] = entry;
}

struct blockEntry* removeBlockFromHashTable(int num) {
    int currHash = num % BLOCK_CACHESIZE;
    int firstHash = currHash;

    while (blockHashTable[currHash] != NULL) {
        if (blockHashTable[currHash]->cacheKey != num) {
            currHash++;
            currHash %= BLOCK_CACHESIZE;

            if (currHash == firstHash) {
                TracePrintf(1, "removeBlockFromHashTable(): key not found\n");
                break;
            }

        } else {
            struct blockEntry* retBlock = blockHashTable[currHash];
            blockHashTable[currHash] = freeBlockEntry;
            return retBlock;
        }
    }
    return NULL;
}


void addBlockToQueue(struct blockMetadata* metadata) {
    if (firstBlock != NULL || lastBlock != NULL) {
        // Add and increment lastBlock
        metadata->prev = lastBlock;
        lastBlock->next = metadata;
        lastBlock = metadata;
        firstBlock->prev = NULL;
        lastBlock->next = NULL;

    } else {
        firstBlock = metadata;
        lastBlock = metadata;
        firstBlock->prev = NULL;
        lastBlock->next = NULL;
    }
}

void removeFirstBlock() {
    if (firstBlock != NULL) {
        if (firstBlock != lastBlock) {
            //Remove firstBlock
            struct blockMetadata* secondBlock = firstBlock->next;
            secondBlock->prev = NULL;
            firstBlock->next = NULL;
            firstBlock->prev = NULL;
            firstBlock = secondBlock;
            return;

        } else {
            TracePrintf(1, "firstBlock = lastBlock;\n");
            firstBlock = NULL;
            lastBlock = NULL;
            return;
        }
    }
    TracePrintf(1, "removeFirstBlock(): queue empty\n");
}

void removeLastBlock() {
    if (lastBlock != NULL) {
        if (firstBlock != lastBlock) {
            //Remove lastBlock
            struct blockMetadata* secondLastBlock = lastBlock->prev;
            secondLastBlock->next = NULL;
            lastBlock->next = NULL;
            lastBlock->prev = NULL;
            lastBlock = secondLastBlock;
            return;

        } else {
            TracePrintf(1, "firstBlock = lastBlock;\n");
            firstBlock = NULL;
            lastBlock = NULL;
            return;
        }
    }
    TracePrintf(1, "removeLastBlock(): queue empty\n");
}


void removeSpecificBlock(struct blockMetadata* metadata) {
    if (metadata->prev == NULL && metadata->next != NULL) {
        removeFirstBlock();
    } else if (metadata->prev != NULL && metadata->next == NULL) {
        removeLastBlock();
    } else if (metadata->prev != NULL && metadata->next != NULL) {
        metadata->prev->next = metadata->next;
        metadata->next->prev = metadata->prev;
        metadata->next = NULL;
        metadata->prev = NULL;
    }
}

struct blockMetadata* fetchLRUBlock(int num) {
    struct blockEntry* lruBlock = fetchBlock(num);
    if (lruBlock != NULL) {
        removeSpecificBlock(lruBlock->metadata);
        addBlockToQueue(lruBlock->metadata);
        return lruBlock->metadata;
    } else {
        return freeBlockMetadata;
    }
}

int syncDiskCache() {
    TracePrintf(1, "syncDiskCache()\n");
    struct inodeMetadata* currInode = firstInode;

    while (currInode != NULL) {
        int inodeNum = currInode->num;
        if (currInode->isDirty == 1) {
            int writeNum = (inodeNum / (BLOCKSIZE / INODESIZE)) + 1;
            struct blockMetadata* readBlock = readBlockFromDisk(writeNum);
            int temp1 = (writeNum - 1) * (BLOCKSIZE / INODESIZE);
            int temp2 = (inodeNum - temp1) * INODESIZE;
            memcpy((void*) (readBlock->data + temp2), (void*)(currInode->value), INODESIZE);
            currInode->isDirty = 0;
            readBlock->isDirty = 1;
        }
        currInode = currInode->next;
    }

    struct blockMetadata* currBlock = firstBlock;

    while (currBlock != NULL) {
        if (currBlock->isDirty == 1) {
            int failure = WriteSector(currBlock->num, (void*) (currBlock->data));
            if (failure != 0) {
                TracePrintf(1, "syncDiskCache(): Error in WriteSector\n");
                return ERROR;
            }
            currBlock->isDirty = 0;
        }
        currBlock = currBlock->next;
    }
    return 0;
}

void removeAndSyncBlock() {
    if (currBlockNum >= BLOCK_CACHESIZE) {
        int removeNum = firstBlock->num;
        syncDiskCache();
        if (firstBlock->isDirty == 1) {
            WriteSector(firstBlock->num, (void*) (firstBlock->data));
        }
        removeFirstBlock();
        removeBlockFromHashTable(removeNum);
        currBlockNum--;
    }
}

void updateLRUBlock(struct blockMetadata* newMetadata, int num) {
    struct blockEntry* currBlock = fetchBlock(num);

    if (currBlock != NULL) {
        removeSpecificBlock(currBlock->metadata);
        addBlockToQueue(newMetadata);
        insertBlockIntoHashTable(newMetadata, num);
    } else {
        removeAndSyncBlock();
        addBlockToQueue(newMetadata);
        insertBlockIntoHashTable(newMetadata, num);
        currBlockNum++;
    }
}

struct blockMetadata* readBlockFromDisk(int num) {
    TracePrintf(1, "readBlockFromDisk()\n");
    struct blockMetadata* lruBlock = fetchLRUBlock(num);
    if (lruBlock->num != -1) {
        return lruBlock;
    } else {
        lruBlock = (struct blockMetadata*) malloc(sizeof(struct blockMetadata));
        ReadSector(num, (void*) lruBlock->data);
        lruBlock->num = num;
        lruBlock->isDirty = 0;
        updateLRUBlock(lruBlock, num);
        struct blockMetadata* newLRUBlock = fetchLRUBlock(num);
        return newLRUBlock;
    }
}

//******************************************************************************

struct inodeEntry* fetchInode(int cacheKey) {
    int currHash = cacheKey % INODE_CACHESIZE;
    int firstHash = currHash;

    while (inodeHashTable[currHash] != NULL) {
        if (inodeHashTable[currHash]->cacheKey != cacheKey) {
            currHash++;
            currHash %= INODE_CACHESIZE;

            if (currHash == firstHash) {
                TracePrintf(1, "fetchInode(): key not found\n");
                break;
            }

        } else {
            return inodeHashTable[currHash];
        }
    }
    return NULL;
}


void insertInodeIntoHashTable(struct inodeMetadata* metadata, int cacheKey) {
    struct inodeEntry* entry = (struct inodeEntry*) malloc(sizeof(struct inodeEntry));
    entry->cacheKey = cacheKey;
    entry->metadata = metadata;
    int currHash = cacheKey % INODE_CACHESIZE;

    while (inodeHashTable[currHash] != NULL && inodeHashTable[currHash]->cacheKey != -1) {
        currHash++;
        currHash %= INODE_CACHESIZE;
    }
    inodeHashTable[currHash] = entry;
}

struct inodeEntry* removeInodeFromHashTable(int num) {
    int currHash = num % INODE_CACHESIZE;
    int firstHash = currHash;

    while (inodeHashTable[currHash] != NULL) {
        if (inodeHashTable[currHash]->cacheKey != num) {
            currHash++;
            currHash %= INODE_CACHESIZE;

            if (currHash == firstHash) {
                TracePrintf(1, "removeInodeFromHashTable(): key not found\n");
                break;
            }

        } else {
            struct inodeEntry* retInode = inodeHashTable[currHash];
            inodeHashTable[currHash] = freeInodeEntry;
            return retInode;
        }
    }
    return NULL;
}

void addInodeToQueue(struct inodeMetadata* metadata) {
    if (firstInode != NULL || lastInode != NULL) {
        // Add and increment lastInode
        metadata->prev = lastInode;
        lastInode->next = metadata;
        lastInode = metadata;
        firstInode->prev = NULL;
        lastInode->next = NULL;

    } else {
        firstInode = metadata;
        lastInode = metadata;
        firstInode->prev = NULL;
        lastInode->next = NULL;
    }
}


void removeFirstInode() {
    if (firstInode != NULL) {
        if (firstInode != lastInode) {
            //Remove firstInode
            struct inodeMetadata* secondInode = firstInode->next;
            secondInode->prev = NULL;
            firstInode->next = NULL;
            firstInode->prev = NULL;
            firstInode = secondInode;

        } else {
            firstInode = NULL;
            lastInode = NULL;
        }
    }
    TracePrintf(1, "removeFirstInode(): queue empty\n");
}

void removeLastInode() {
    if (lastInode != NULL) {
        if (firstInode != lastInode) {
            //Remove lastInode
            struct inodeMetadata* secondLastInode = lastInode->prev;
            secondLastInode->next = NULL;
            lastInode->next = NULL;
            lastInode->prev = NULL;
            lastInode = secondLastInode;

        } else {
            firstInode = NULL;
            lastInode = NULL;
        }
    }
    TracePrintf(1, "removeLastInode(): queue empty\n");
}

void removeSpecificInode(struct inodeMetadata* metadata) {  
    if (metadata->prev == NULL && metadata->next != NULL) {
        removeFirstInode();
    } else if (metadata->prev != NULL && metadata->next == NULL) {
        removeLastInode();
    } else if (metadata->prev != NULL && metadata->next != NULL) {
        metadata->prev->next = metadata->next;
        metadata->next->prev = metadata->prev;
        metadata->next = NULL;
        metadata->prev = NULL;
    }
}

struct inodeMetadata* fetchLRUInode(int num) {
    struct inodeEntry* lruInode = fetchInode(num);
    if (lruInode != NULL) {
        removeSpecificInode(lruInode->metadata);
        addInodeToQueue(lruInode->metadata);
        return lruInode->metadata;
    } else {
        return freeInodeMetadata;
    }
}

void updateLRUInode(struct inodeMetadata* newMetadata, int num) {
    struct inodeEntry* cache = fetchInode(num);

    if (cache != NULL) {
        removeSpecificInode(cache->metadata);
        addInodeToQueue(newMetadata);
        insertInodeIntoHashTable(newMetadata, num);
    } else {
        if (currInodeNum >= INODE_CACHESIZE) {
            int removeNum = firstInode->num;
            removeInodeFromHashTable(removeNum);
            syncDiskCache();
            if (firstInode->isDirty == 1) {
                int blockNum =  removeNum / (BLOCKSIZE / INODESIZE) + 1;
                struct blockMetadata* newBlock = readBlockFromDisk(blockNum);
                int temp1 = (blockNum - 1) * (BLOCKSIZE / INODESIZE);
                int temp2 = (num - temp1) * INODESIZE;
                memcpy((void*) (newBlock->data + temp2), (void*) firstInode->value, INODESIZE);
                newBlock->isDirty = 1;
            }
            removeFirstInode();
            removeInodeFromHashTable(removeNum);
            currInodeNum--;
        }
        addInodeToQueue(newMetadata);
        insertInodeIntoHashTable(newMetadata, num);
        currInodeNum++;
    }
}

struct inodeMetadata* readInodeFromDisk(int num) {
    TracePrintf(1, "readInodeFromDisk()\n");
    struct inodeMetadata* lruInode = fetchLRUInode(num);

    if (lruInode->num != -1) {
        return lruInode;
    } else {
        int blockNum =  num / (BLOCKSIZE / INODESIZE) + 1;
        struct blockMetadata* newBlock = fetchLRUBlock(blockNum);
        if (newBlock->num == -1) {
            newBlock = readBlockFromDisk(blockNum);
        }
        lruInode = (struct inodeMetadata*) malloc(sizeof(struct inodeMetadata));
        lruInode->isDirty = 0;
        lruInode->num = num;
        lruInode->value = (struct inode*) malloc(sizeof(struct inode));
        int temp1 = (blockNum - 1) * (BLOCKSIZE / INODESIZE);
        int temp2 = (num - temp1) * INODESIZE;
        memcpy(lruInode->value, newBlock->data + temp2, INODESIZE);
        updateLRUInode(lruInode, num);
        return lruInode;
    }
}