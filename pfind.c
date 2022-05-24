#include <threads.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <pthread.h>

#define SUCCESS 0
#define Failure 1

/* run this :
gcc -O3 -D_POSIX_C_SOURCE=200809 -Wall -std=c11 -pthread pfind.c
*/
typedef struct qNode
{
    char path_fileName[PATH_MAX];
    struct qNode *next;
} qNode;

typedef struct dirQueue
{
    struct qNode *head;
    struct qNode *tail;
    size_t size;
} dirQueue;

int mails = 0;
int valueMatchCnt = 0;
int threadsWithErr = 0;
int sleepThreadCnt = 0;
struct dirQueue globalQueue;
int threadsInitCnt = 0;
int threadWaitCnt = 0;
int numOfThreads;

char *searchValue;
pthread_mutex_t mutex;
pthread_mutex_t valueMatchCntLock;
pthread_mutex_t threadsWithErrLock;
pthread_mutex_t queueLock;
pthread_mutex_t waitInit;
pthread_cond_t initThreads;
pthread_cond_t emptyQueCond;

int insertDir(qNode *createDir){
    if(globalQueue.head == NULL && globalQueue.tail == NULL)
    {
        globalQueue.head =createDir;
        globalQueue.tail = createDir;
        globalQueue.size++;
        return 1;
    }
    globalQueue.tail->next = createDir;
    globalQueue.tail = createDir;
    globalQueue.size++;
    return 0;
}

qNode* removeHeadDir(){
    if(globalQueue.head == NULL && globalQueue.tail == NULL)
    {
        return NULL;
    }
    qNode *tmp = globalQueue.head;
    if (globalQueue.size == 1 && globalQueue.head == globalQueue.tail)
    {
        globalQueue.head = NULL;
        globalQueue.tail = NULL;
        globalQueue.size--;
        return tmp;
    }
    globalQueue.head = globalQueue.head->next;
    globalQueue.size--;
    return tmp;
}

qNode* createQNode(char* dirName)
{
    qNode * node = malloc(sizeof(qNode));
    if (node == NULL)
    {
        exit(1);
    }
    strcpy(node->path_fileName, dirName);
    node->next = NULL;
    return node;
}

int directorySearch(char* dir)
{
    
    char nodeDirPath[PATH_MAX];
    DIR *openedDir = opendir(dir);
    struct dirent *dirent;
    struct stat statbuf;
    qNode *tmpDir;
    if (dir == NULL){
        fprintf(stderr, "Directory %s: Permission denied.\n", dir);
        return Failure;
    }
    while ((dirent = readdir(openedDir)) != NULL)
    {
        if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0){
            continue;
        }
        sprintf(nodeDirPath, "%s/%s", dir, dirent->d_name);
        if (stat(nodeDirPath, &statbuf) != SUCCESS){
            fprintf(stderr, "stat failed on %s, error: %s\n",nodeDirPath, strerror(errno));
            return Failure;
        }
        /*check if we found a directory */
        if (S_ISDIR(statbuf.st_mode))
        {
            /*check if we have premissions*/
            if (opendir(nodeDirPath) != NULL)
            {
                pthread_mutex_lock(&queueLock);
                tmpDir = createQNode(nodeDirPath);
                insertDir(tmpDir);
                pthread_cond_broadcast(&emptyQueCond);
                pthread_mutex_unlock(&queueLock);
            }
            else
            {
                printf("Directory %s: Permission denied.\n", nodeDirPath);
            }
        }
        else
        {
            /*found file and its name is equal to the name of the file that we want*/
            if (S_ISREG(statbuf.st_mode) && strstr(dirent->d_name, searchValue))
            {
                /*printf("%s\n", dir);*/
                pthread_mutex_lock(&valueMatchCntLock);
                valueMatchCnt++;
                pthread_mutex_unlock(&valueMatchCntLock);
            }
        }
    }    
    closedir(openedDir);
    return SUCCESS;
}

void* directoryThreadSearch()
{
    pthread_mutex_lock(&waitInit);
    threadsInitCnt++;
    int flag = 0;
    if (threadsInitCnt == numOfThreads)
    {
        pthread_cond_broadcast(&initThreads);
        /*printf("starting Together \n");*/
    }
    else
    {
         pthread_cond_wait(&initThreads, &waitInit);
    }
    pthread_mutex_unlock(&waitInit);

    /* handling searching directories starts from here*/
    while (1)
    {
        pthread_mutex_lock(&queueLock);
        /* queue is empty*/
        while (globalQueue.size == 0)
        {
            if (!flag){
                flag = 1;
                sleepThreadCnt++;
            }
            // Queue is empty and all threads are idle - means we are done
            if (sleepThreadCnt + threadsWithErr == numOfThreads){
                pthread_cond_broadcast(&initThreads);
                pthread_cond_broadcast(&emptyQueCond);
                pthread_mutex_unlock(&queueLock);
                pthread_exit(NULL);
            }
            // Otherwise, we wait for work
            pthread_cond_wait(&emptyQueCond, &queueLock);
        }
        
        if (flag){
            flag = 0;
            pthread_mutex_lock(&threadsWithErrLock);
            sleepThreadCnt--;
            pthread_mutex_unlock(&threadsWithErrLock);
        }
        qNode* node = removeHeadDir();
        if (node == NULL)
        {
            fprintf(stderr, "error occured, dequed from an empty queue\n");
            pthread_mutex_lock(&threadsWithErrLock);
            threadsWithErr++;
            pthread_mutex_unlock(&threadsWithErrLock);
            pthread_mutex_unlock(&queueLock);
            pthread_mutex_unlock(&waitInit);
            pthread_cond_broadcast(&initThreads);
            pthread_cond_broadcast(&emptyQueCond);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&queueLock);
        if (directorySearch(node->path_fileName) != SUCCESS)
        {
            pthread_mutex_lock(&threadsWithErrLock);
            threadsWithErr++;
            pthread_mutex_unlock(&threadsWithErrLock);
        }
        free(node);
    }
    
    return NULL;
}
    /*
    for (int i = 0; i < 10000000; i++)
    {
        pthread_mutex_lock(&queueLock);
        mails++;
        pthread_mutex_unlock(&queueLock);
    }
    */



int main(int argc, char *argv[])
{
    DIR *dir;
    
    if (argc != 4)
    {
        fprintf(stderr, "Invalid Number of arguemens");
        return 1;
    }
    /*
    searchValue = "a.out";
    int j = directorySearch(argv[1]);
    printf("%d\n", j);
    printf("%d\n", valueMatchCnt);
    */
    dir = opendir(argv[1]);
    if (dir == NULL)
    {
        return 1;
    }
    
     /*getting the relevant inputs */
    searchValue = argv[2];
    numOfThreads = atoi(argv[3]);

    /* Queue putting the head */
    qNode *headNode = createQNode(argv[1]);
    globalQueue.size = 0;
    insertDir(headNode);
    /*
    printf("%ld\n", globalQueue.size);
    qNode *remNode = removeHeadDir();
    printf("%s \n", remNode->path_fileName);
    printf("%ld\n", globalQueue.size);
    printf("%s \n", globalQueue.head->path_fileName);
    printf("%s \n", globalQueue.tail->path_fileName);
    */
    

   /*init all mutex and conds*/
    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&valueMatchCntLock, NULL);
    pthread_mutex_init(&threadsWithErrLock, NULL);
    pthread_mutex_init(&waitInit, NULL);
    pthread_mutex_init(&queueLock, NULL);
    pthread_cond_init(&initThreads,NULL);
    pthread_cond_init(&emptyQueCond,NULL);

    /* intializing all the threads*/
    pthread_t threadsArr[numOfThreads];
    for (int i = 0; i < numOfThreads; i++)
    {
        if(pthread_create(&threadsArr[i], NULL, directoryThreadSearch, NULL))
        {
            perror("failed to create thread \n");
            return 1;
        }
        /*printf("thread %d has started \n", i);*/
    }

    /* */
    pthread_cond_broadcast(&initThreads);
    /* join the threads */
    for (int i = 0; i < numOfThreads; i++)
    {
        if(pthread_join(threadsArr[i], NULL) != 0)
        {
            perror("failed to join thread \n");
            return 2;
        }
        /*printf("thread %d has finished executing \n", i);*/
    }
    /* destroy the mutexes*/
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&queueLock);
    pthread_mutex_destroy(&waitInit);
    pthread_mutex_destroy(&valueMatchCntLock);
    pthread_mutex_destroy(&threadsWithErrLock);
    pthread_cond_destroy(&initThreads);
    pthread_cond_destroy(&emptyQueCond);

    printf("Done searching, found %d files\n", valueMatchCnt);
    if (threadsWithErr > 0)
    {
        exit(Failure);
    }


    /* close the dir*/
    closedir(dir);
    return 1;
}