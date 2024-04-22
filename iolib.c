#include <comp421/hardware.h>
#include <comp421/filesystem.h>
#include <comp421/iolib.h>
#include <comp421/yalnix.h>

#include "cacheMgmt.h"

struct openFileInfo {
    short inodeNum;
    int pos;
};

struct openFileInfo oft[MAX_OPEN_FILES];

int oftCreated;
int currNumOpenFile;
short dirName;

void createOFT() {
    int i;
    for (i = 0; i < MAX_OPEN_FILES; i++) {
        oft[i].inodeNum = -1;
        oft[i].pos = 0;
    }
    oftCreated = 1;
    currNumOpenFile = 0;
    dirName = ROOTINODE;
}

int processPathname(char* oldPath, char* newPath) {
    int oldLen = strlen(oldPath);
    int newLen = 0;
    int ptr1;

    for (ptr1 = 0; ptr1 < oldLen; ptr1++) {
        if (oldPath[ptr1] != '/' || oldPath[ptr1 + 1] != '/') {
            newPath[newLen] = oldPath[ptr1];
            newLen++;
        }
    }

    if (newPath[newLen - 1] != '/') {
        newPath[newLen] = '\0';
    } else {
        newPath[newLen] = '.';
        newPath[newLen + 1] = '\0';
        newLen++;
    }
    return newLen;
    
}

int requestFileSys(void** args, int argNum, int* argSize, uint8_t opCode) {
    int diff = 1;
    char msg[32];
    msg[0] = (char) opCode;

    int i;
    for (i = 0; i < argNum; i++) {
        if (argSize[i] != sizeof(void*) || *(void**) args[i] != NULL) {
            memcpy(msg + diff, (char**) args[i], argSize[i]);
            diff += argSize[i];
            
        } else {
            return ERROR;
        }
    }
    int sendRes = Send(msg, -FILE_SERVER);
    int reply = *(int*) msg;

    if (sendRes != -1 && reply != -1) {
        return reply;
    } else {
        if (opCode == 1) {
            printf("iolib: Error encountered in Open()\n");
        } else if (opCode == 2) {
            printf("iolib: Error encountered in Close()\n");
        } else if (opCode == 3) {
            printf("iolib: Error encountered in Create()\n");
        } else if (opCode == 4) {
            printf("iolib: Error encountered in Read()\n");
        } else if (opCode == 5) {
            printf("iolib: Error encountered in Write()\n");
        } else if (opCode == 6) {
            printf("iolib: Error encountered in Seek()\n");
        } else if (opCode == 7) {
            printf("iolib: Error encountered in Link()\n");
        } else if (opCode == 8) {
            printf("iolib: Error encountered in Unlink()\n");
        } else if (opCode == 9) {
            printf("iolib: Error encountered in SymLink()\n");
        } else if (opCode == 10) {
            printf("iolib: Error encountered in ReadLink()\n");
        } else if (opCode == 11) {
            printf("iolib: Error encountered in MkDir()\n");
        } else if (opCode == 12) {
            printf("iolib: Error encountered in RmDir()\n");
        } else if (opCode == 13) {
            printf("iolib: Error encountered in ChDir()\n");
        } else if (opCode == 14) {
            printf("iolib: Error encountered in Stat()\n");
        } else if (opCode == 15) {
            printf("iolib: Error encountered in Sync()\n");
        } else if (opCode == 16) {
            printf("iolib: Error encountered in Shutdown()\n");
        }
        return reply;
    }
}

int Open(char *pathname) {
    if (!oftCreated) {
        createOFT();
    }
    if (currNumOpenFile >= MAX_OPEN_FILES) {
        printf("iolib: Open(): Cannot open file because max number of files open\n");
        return 0;
    }

    char *newPath = (char*) malloc(strlen(pathname) + 2);
    int pathLen = processPathname(pathname, newPath);
    void* args[3] = {(void*) &newPath, (void*) &pathLen, (void*) &dirName};
    int argSize[3] = {sizeof(newPath), sizeof(pathLen), sizeof(dirName)};
    int resp = requestFileSys(args, 3, argSize, 1);

    free(newPath);

    int i;
    for (i = 0; i < MAX_OPEN_FILES; i++) {
        if (oft[i].inodeNum == -1) {
            currNumOpenFile++;
            oft[i].inodeNum = resp;
            return i;
        }
    }
    TracePrintf(1, "iolib: Open(): Catch all error\n");
    return ERROR;
}

int Close(int fd) {
    if (!oftCreated) {
        createOFT();
    }
    if (oft[fd].inodeNum != -1 && fd < MAX_OPEN_FILES && fd >= 0) {
        oft[fd].inodeNum = -1;
        oft[fd].pos = 0;
        currNumOpenFile--;
        return 0;
    } else {
        printf("iolib: Close(): fd is not the descriptor number of a file currently open\n");
        return ERROR;
    }
}

int Create(char *pathname) {
    if (!oftCreated) {
        createOFT();
    }
    if (currNumOpenFile >= MAX_OPEN_FILES) {
        printf("iolib: Create(): Cannot open file because max number of files open\n");
        return 0;
    }

    char *newPath = (char*) malloc(strlen(pathname) + 2);
    int pathLen = processPathname(pathname, newPath);
    void* args[3] = {(void*) &newPath, (void*) &pathLen, (void*) &dirName};
    int argSize[3] = {sizeof(newPath), sizeof(pathLen), sizeof(dirName)};
    int resp = requestFileSys(args, 3, argSize, 3);

    free(newPath);

    int i;
    for (i = 0; i < MAX_OPEN_FILES; i++) {
        if (oft[i].inodeNum == -1) {
            currNumOpenFile++;
            oft[i].inodeNum = resp;
            return i;
        }
    }
    TracePrintf(1, "iolib: Create(): Catch all error\n");
    return ERROR;
}

int Read(int fd, void *buf, int size) {
    if (!oftCreated) {
        createOFT();
    }

    if (oft[fd].inodeNum != -1 && fd < MAX_OPEN_FILES && fd >= 0) {
        void* args[4] = {(void*) &buf, (void*) &size, (void*) &oft[fd].inodeNum, (void*) &oft[fd].pos};
        int argSize[4] = {sizeof(buf), sizeof(size), sizeof(oft[fd].inodeNum), sizeof(oft[fd].pos)};
        int resp = requestFileSys(args, 4, argSize, 4);

        if (resp != -1) {
            oft[fd].pos += resp;
            return resp;
        }
        TracePrintf(1, "iolib: Read(): Error in response\n");
        return ERROR;

    } else {
        printf("iolib: Read(): fd is not valid\n");
        return ERROR;
    }
}

int Write(int fd, void *buf, int size) {
    if (!oftCreated) {
        createOFT();
    }

    if (oft[fd].inodeNum != -1 && fd < MAX_OPEN_FILES && fd >= 0) {
        void* args[4] = {(void*) &buf, (void*) &size, (void*) &oft[fd].inodeNum, (void*) &oft[fd].pos};
        int argSize[4] = {sizeof(buf), sizeof(size), sizeof(oft[fd].inodeNum), sizeof(oft[fd].pos)};
        int resp = requestFileSys(args, 4, argSize, 5);

        if (resp != -1) {
            oft[fd].pos += resp;
            return resp;
        }
        TracePrintf(1, "iolib: Write(): Error in response\n");
        return ERROR;

    } else {
        printf("iolib: Write(): fd is not valid\n");
        return ERROR;
    }
}

int Seek(int fd, int offset, int whence) {
    if (!oftCreated) {
        createOFT();
    }

    if (oft[fd].inodeNum != -1 && fd < MAX_OPEN_FILES && fd >= 0) {
        int newPos = -1;

        if (whence == SEEK_SET) {
            newPos = offset;
        } else if (whence == SEEK_CUR) {
            newPos = oft[fd].pos + offset;
        } else if (whence == SEEK_END) {
            void* args[1] = {(void*) &oft[fd].inodeNum};
            int argSize[1] = {sizeof(oft[fd].inodeNum)};
            int resp = requestFileSys(args, 1, argSize, 6);
            newPos = resp - offset;
        } else {
            printf("iolib: Seek(): whence not one of 3 defined values\n");
        }

        if (newPos >= 0) {
            oft[fd].pos = newPos;
            return 0;
        }
        TracePrintf(1, "iolib: Seek(): Negative pos value\n");
        return ERROR;

    } else {
        printf("iolib: Seek(): fd is not valid\n");
        return ERROR;
    }
}

int Link(char *oldname, char *newname) {
    char *newPath1 = (char*) malloc(strlen(oldname) + 2);
    char *newPath2 = (char*) malloc(strlen(newname) + 2);

    int newLen1 = processPathname(oldname, newPath1);
    int newLen2 = processPathname(newname, newPath2);

    void* args[5] = {(void*) &oldname, (void*) &newLen1, (void*) &newname, (void*) &newLen2, (void*) &dirName};
    int argSize[5] = {sizeof(oldname), sizeof(newLen1), sizeof(newname), sizeof(newLen2), sizeof(dirName)};
    int resp = requestFileSys(args, 5, argSize, 7);

    free(newPath1);
    free(newPath2);
    if (resp != ERROR) {
        return resp;
    }
    TracePrintf(1, "iolib: Link(): Error in response\n");
    return ERROR;
}

int Unlink(char *pathname) {
    char *newPath = (char*) malloc(strlen(pathname) + 2);
    int newLen = processPathname(pathname, newPath);

    void* args[3] = {(void*) &pathname, (void*) &newLen, (void*) &dirName};
    int argSize[3] = {sizeof(pathname), sizeof(newLen), sizeof(dirName)};
    int resp = requestFileSys(args, 3, argSize, 8);

    free(newPath);
    if (resp != ERROR) {
        return resp;
    }
    TracePrintf(1, "iolib: Unlink(): Error in response\n");
    return ERROR;
}

int SymLink(char *oldname, char *newname) {
    char *newPath1 = (char*) malloc(strlen(oldname) + 2);
    char *newPath2 = (char*) malloc(strlen(newname) + 2);

    int newLen1 = processPathname(oldname, newPath1);
    int newLen2 = processPathname(newname, newPath2);

    void* args[5] = {(void*) &oldname, (void*) &newLen1, (void*) &newname, (void*) &newLen2, (void*) &dirName};
    int argSize[5] = {sizeof(oldname), sizeof(newLen1), sizeof(newname), sizeof(newLen2), sizeof(dirName)};
    int resp = requestFileSys(args, 5, argSize, 9);

    free(newPath1);
    free(newPath2);
    if (resp != ERROR) {
        return 0;
    }
    TracePrintf(1, "iolib: SymLink(): Error in response\n");
    return ERROR;
}

int ReadLink(char *pathname, char *buf, int len) {
    char *newPath = (char*) malloc(strlen(pathname) + 2);
    int newLen = processPathname(pathname, newPath);

    void* args[5] = {(void*) &pathname, (void*) &newLen, (void*) &buf, (void*) &len, (void*) &dirName};
    int argSize[5] = {sizeof(pathname), sizeof(newLen), sizeof(buf), sizeof(len), sizeof(dirName)};
    int resp = requestFileSys(args, 5, argSize, 10);

    free(newPath);
    if (resp != ERROR) {
        return resp;
    }
    TracePrintf(1, "iolib: ReadLink(): Error in response\n");
    return ERROR;
}

int MkDir(char *pathname) {
    char *newPath = (char*) malloc(strlen(pathname) + 2);
    int newLen = processPathname(pathname, newPath);

    void* args[3] = {(void*) &pathname, (void*) &newLen, (void*) &dirName};
    int argSize[3] = {sizeof(pathname), sizeof(newLen), sizeof(dirName)};
    int resp = requestFileSys(args, 3, argSize, 11);

    free(newPath);
    if (resp != ERROR) {
        return resp;
    }
    TracePrintf(1, "iolib: MkDir(): Error in response\n");
    return ERROR;
}

int RmDir(char *pathname) {
    char *newPath = (char*) malloc(strlen(pathname) + 2);
    int newLen = processPathname(pathname, newPath);

    void* args[2] = {(void*) &pathname, (void*) &newLen};
    int argSize[2] = {sizeof(pathname), sizeof(newLen)};
    int resp = requestFileSys(args, 2, argSize, 12);

    free(newPath);
    if (resp != ERROR) {
        return resp;
    }
    TracePrintf(1, "iolib: RmDir(): Error in response\n");
    return ERROR;
}

int ChDir(char *pathname) {
    char *newPath = (char*) malloc(strlen(pathname) + 2);
    int newLen = processPathname(pathname, newPath);

    void* args[3] = {(void*) &pathname, (void*) &newLen, (void*) &dirName};
    int argSize[3] = {sizeof(pathname), sizeof(newLen), sizeof(dirName)};
    int resp = requestFileSys(args, 3, argSize, 13);

    free(newPath);
    if (resp != ERROR) {
        dirName = resp;
        return 0;
    }
    TracePrintf(1, "iolib: ChDir(): Error in response\n");
    return ERROR;
}

int Stat(char *pathname, struct Stat *statbuf) {
    char *newPath = (char*) malloc(strlen(pathname) + 2);
    int newLen = processPathname(pathname, newPath);

    void* args[4] = {(void*) &pathname, (void*) &newLen, (void*) &statbuf, (void*) &dirName};
    int argSize[4] = {sizeof(pathname), sizeof(newLen), sizeof(statbuf), sizeof(dirName)};
    int resp = requestFileSys(args, 4, argSize, 14);

    free(newPath);
    if (resp != ERROR) {
        return resp;
    }
    TracePrintf(1, "iolib: Stat(): Error in response\n");
    return ERROR;
}

int Sync(void) {
    return requestFileSys(NULL, 0, NULL, 15);
}

int Shutdown(void) {
    return requestFileSys(NULL, 0, NULL, 16);
}

