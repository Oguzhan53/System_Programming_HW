
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>

#define PerSize 9 // Permission input size

__sig_atomic_t signalCount = 0; // SIGINT flag
/*
*   SIGINT signal handler
*/
void handler(int signal)
{
    ++signalCount;
}

/*
*   Fıle type enum
*/
typedef enum fileType
{
    Dir,
    Sckt,
    BlckDv,
    ChrDv,
    RegFile,
    Fifo,
    SymnLnk,
    Empty
} fileType;

/*
*   Stack node
*/
struct node
{
    char *data;
    struct node *next;
};
typedef struct node node;

/*
*   The tree stack
*/
node *head;

void append(char **source, char *extension);
int traverse(char *directory, char *flName, long byt, fileType flType, char *permission, int lnkNum, char *leaf);
void replace(char **directory, char *extension);
int compare(struct stat stats, char *directory, char *flName, long byt, fileType flType, char *permission, int lnkNum, char *leaf);
void freePtr(char *ptr);
void addleaf(char **leaf);
void push(char *data);
char *pop();
char *takeName(char *directory, char *leaf);
void cleanStack();
int checkFileType(char *flTyep);
int checkPermission(char *perm);

int main(int argc, char *argv[])
{

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &handler;
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("Failed to install SIGINT signal handler");
        return -1;
    }

    head = NULL;

    int opt;

    char *fName = NULL, *fType = NULL, *perm = NULL, *directory = NULL;
    int linkNum = -1, fSize = -1;
    while ((opt = getopt(argc, argv, "b:f:t:p:l:w:")) != -1)
    {
        switch (opt)
        {

        case 'f':

            if (optarg != NULL)
            {

                fName = (char *)malloc(strlen(optarg) + 1);
                strcpy(fName, optarg);
            }
            break;
        case 'b':

            if (optarg != NULL)
            {
                if (atoi(optarg) <= 0)
                    fSize = -2;
                else
                    fSize = atoi(optarg);
            }

            break;

        case 't':

            if (optarg != NULL)
            {

                fType = (char *)malloc(strlen(optarg) + 1);
                strcpy(fType, optarg);
            }
            break;
        case 'p':

            if (optarg != NULL)
            {

                perm = (char *)malloc(strlen(optarg) + 1);
                strcpy(perm, optarg);
            }
            break;
        case 'l':

            if (optarg != NULL)
            {
                if (atoi(optarg) <= 0)
                    linkNum = -2;
                else
                    linkNum = atoi(optarg);
            }
            break;
        case 'w':

            if (optarg != NULL)
            {
                directory = (char *)malloc(strlen(optarg) + 1);
                strcpy(directory, optarg);
            }
            break;

        case '?':
            fprintf(stderr, "Exiting...\n");
            freePtr(fName);
            freePtr(fType);
            freePtr(perm);
            freePtr(directory);
            exit(-1);
            break;
        }
    }

    int numType = checkFileType(fType);

    if (fSize == -2 || linkNum == -2 || checkPermission(perm) == -1 || numType == -1 || directory == NULL)
    {

        freePtr(fName);
        freePtr(fType);
        freePtr(perm);
        freePtr(directory);
        printf("Invalid Input \n");
        return -1;
    }

    char *leaf = (char *)malloc(2);
    memset(leaf, '\0', 2);
    strcpy(leaf, "|");
    int res = traverse(directory, fName, fSize, numType, perm, linkNum, leaf);
    if (res == -1)
    {
        printf("File not found \n");
        freePtr(directory);
        freePtr(leaf);
        freePtr(fName);
        freePtr(fType);
        freePtr(perm);
        cleanStack();
        return -1;
    }

    char *buff;
    if (head != NULL)
    {
        while (head->next != NULL)
        {
            buff = pop();
            printf("%s\n", buff);
            freePtr(buff);
        }

        buff = pop();
        printf("%s\n", buff);
        freePtr(buff);
    }

    freePtr(leaf);
    freePtr(fName);
    freePtr(fType);
    freePtr(perm);
    freePtr(directory);

    return 0;
}

/*
*   This function free the stack resources 
*/
void cleanStack()
{
    if (head != NULL)
    {
        char *buff;
        while (head->next != NULL)
        {
            buff = pop();
            freePtr(buff);
        }
        buff = pop();
        freePtr(buff);
    }
}

/*
*   This function removes file names from the tree stack 
*/
char *pop()
{
    if (head != NULL)
    {
        node *newHead;
        newHead = head;
        head = head->next;
        int len = strlen(newHead->data);
        char *data = (char *)malloc(sizeof(char) * len + 1);
        strcpy(data, newHead->data);
        freePtr(newHead->data);
        free(newHead);
        return data;
    }
    return NULL;
}

/*
*   This function adds file names to the tree stack 
*/
void push(char *data)
{
    node *newHead;
    newHead = malloc(sizeof(node));
    int len = strlen(data);
    newHead->data = (char *)malloc(sizeof(char) * len + 1);
    memset(newHead->data, '\0', len + 1);
    strcpy(newHead->data, data);
    newHead->next = head;
    head = newHead;
}

/*
*   This function checks validation of the permission input
*/
int checkPermission(char *perm)
{
    if (perm == NULL)
        return 0;
    if (strlen(perm) != PerSize)
        return -1;
    for (size_t i = 0; i < PerSize; i++)
    {
        if (i % 3 == 0 && !(perm[i] == 'r' || perm[i] == '-'))
        {
            return -1;
        }
        else if (i % 3 == 1 && !(perm[i] == 'w' || perm[i] == '-'))
        {
            return -1;
        }
        else if (i % 3 == 2 && !(perm[i] == 'x' || perm[i] == '-'))
        {
            return -1;
        }
    }
    return 0;
}

/*
*   This function checks validation of the file type input
*/
int checkFileType(char *flTyep)
{
    if (flTyep == NULL)
        return 7;
    if (strlen(flTyep) != 1)
        return -1;
    char *types = "dsbcfpl";
    for (size_t i = 0; i < 7; i++)
    {
        if (types[i] == flTyep[0])
            return i;
    }

    return -1;
}

/*
*   This function free the char pointers
*/
void freePtr(char *ptr)
{
    if (ptr != NULL)
        free(ptr);
}

/*
*   This function compare the file properties with the target properties
*/
int compare(struct stat stats, char *directory, char *flName, long byt, fileType flType, char *permission, int lnkNum, char *leaf)
{
    if (directory == NULL)
        return -1;
    int dsize = strlen(directory) + 1;

    int i = dsize - 1;
    int flag = 0;
    for (; i > -1; i--)
    {
        if (directory[i] == '/')
            break;
    }
    char *name = (char *)malloc(sizeof(char) * (dsize - i + 1));
    memset(name, '\0', (dsize - i + 1));
    i++;
    int i2 = 0;
    for (; i < dsize; i++)
    {
        name[i2] = directory[i];
        i2++;
    }

    if (flName != NULL && name != NULL && strcasecmp(flName, name) != 0)
    {
        flag = -1;
    }

    if (byt != -1 && stats.st_size != byt)
    {
        flag = -1;
    }

    if (flType != Empty)
    {
        if (S_ISDIR(stats.st_mode) && flType != Dir)
        {

            flag = -1;
        }
        else if (S_ISCHR(stats.st_mode) && flType != ChrDv)
        {

            flag = -1;
        }
        else if (S_ISBLK(stats.st_mode) && flType != BlckDv)
        {
            flag = -1;
        }
        else if (S_ISFIFO(stats.st_mode) && flType != Fifo)
        {
            flag = -1;
        }
        else if (S_ISLNK(stats.st_mode) && flType != SymnLnk)
        {
            flag = -1;
        }
        else if (S_ISSOCK(stats.st_mode) && flType != Sckt)
        {
            flag = -1;
        }
        else if (S_ISREG(stats.st_mode) && flType != RegFile)
        {
            flag = -1;
        }
    }

    if (permission != NULL)
    {
        char tper[PerSize];
        memset(tper, '-', 9);
        if (stats.st_mode & S_IRUSR)
            tper[0] = 'r';
        if (stats.st_mode & S_IWUSR)
            tper[1] = 'w';
        if (stats.st_mode & S_IXUSR)
            tper[2] = 'x';
        if (stats.st_mode & S_IRGRP)
            tper[3] = 'r';
        if (stats.st_mode & S_IWGRP)
            tper[4] = 'w';
        if (stats.st_mode & S_IXGRP)
            tper[5] = 'x';
        if (stats.st_mode & S_IROTH)
            tper[6] = 'r';
        if (stats.st_mode & S_IWOTH)
            tper[7] = 'w';
        if (stats.st_mode & S_IXOTH)
            tper[8] = 'x';

        for (size_t i3 = 0; i3 < PerSize; i3++)
        {
            if (tper[i3] != permission[i3])
            {
                flag = -1;
                break;
            }
        }
    }

    if (lnkNum != -1 && lnkNum != stats.st_nlink)
    {
        flag = -1;
    }
    if (flag == -1)
    {
        freePtr(name);
        return -1;
    }

    char *tempLeaf = (char *)malloc(sizeof(char) * strlen(leaf) + 1);
    memset(tempLeaf, '\0', (strlen(leaf) + 1));
    strcpy(tempLeaf, leaf);
    addleaf(&tempLeaf);

    char *addName = takeName(directory, leaf);

    push(addName);

    freePtr(addName);

    freePtr(tempLeaf);
    freePtr(name);
    return 0;
}

/*
*   This function traverses the directory
*/
int traverse(char *directory, char *flName, long byt, fileType flType, char *permission, int lnkNum, char *leaf)
{

    if (directory == NULL)
        return -1;

    struct dirent *dir;
    int flag = 0, flag2 = -1;
    int t = 0, t1 = -1, f1 = 0;

    char *tempDir = (char *)malloc(sizeof(char) * (strlen(directory) + 1));
    if (tempDir != NULL)
    {
        strcpy(tempDir, directory);
    }
    DIR *dr = opendir(tempDir);

    if (dr == NULL)
    {
        closedir(dr);
        free(tempDir);

        return -1;
    }

    char *tempLeaf = (char *)malloc(sizeof(char) * strlen(leaf) + 1);
    memset(tempLeaf, '\0', (strlen(leaf) + 1));
    strcpy(tempLeaf, leaf);
    addleaf(&tempLeaf);
    while ((dir = readdir(dr)) != NULL)
    {
        if (signalCount != 0)
        {
            printf("\nSIGINT Signal arrived. The program is closing...\n");
            freePtr(tempLeaf);
            freePtr(tempDir);
            closedir(dr);
            cleanStack();
            return -1;
        }
        struct stat stats;
        if (dir->d_name != NULL)
        {
            if ((strcmp(".", dir->d_name) == 0) || (strcmp("..", dir->d_name) == 0))
                continue;
            else
            {
                char *addname2;

                if (flag == 0)
                {
                    flag = 1;

                    append(&tempDir, dir->d_name);
                    if (lstat(tempDir, &stats) == 0)
                    {
                        if (S_ISDIR(stats.st_mode))
                        {
                            f1 = traverse(tempDir, flName, byt, flType, permission, lnkNum, tempLeaf);
                            if (flag2 == -1)
                                flag2 = f1;
                        }
                        t = compare(stats, tempDir, flName, byt, flType, permission, lnkNum, tempLeaf);

                        if (t1 == -1)
                        {
                            addname2 = takeName(directory, leaf);
                            t1 = t;
                            if (t == 0)
                            {
                                push(addname2);
                            }
                            freePtr(addname2);
                        }
                    }
                }
                else
                {

                    replace(&tempDir, dir->d_name);
                    if (lstat(tempDir, &stats) == 0)
                    {
                        if (S_ISDIR(stats.st_mode))
                        {
                            f1 = traverse(tempDir, flName, byt, flType, permission, lnkNum, tempLeaf);
                            if (flag2 == -1)
                                flag2 = f1;
                        }
                        t = compare(stats, tempDir, flName, byt, flType, permission, lnkNum, tempLeaf);

                        if (t1 == -1)
                        {
                            addname2 = takeName(directory, leaf);
                            t1 = t;
                            if (t == 0)
                            {
                                push(addname2);
                            }
                            freePtr(addname2);
                        }
                    }
                }
            }
        }
    }
    char *addName = takeName(directory, leaf);

    if (t != 0 && flag2 == 0)
    {

        push(addName);
        t1 = 0;
    }
    freePtr(addName);
    freePtr(tempLeaf);
    freePtr(tempDir);
    closedir(dr);
    return t1;
}

/*
*   This function takes the filename from the end of the path
*/
char *takeName(char *directory, char *leaf)
{
    if (directory != NULL && leaf != NULL)
    {
        int dsize = strlen(directory) + 1;

        int i = dsize - 1;
        for (; i > -1; i--)
        {
            if (directory[i] == '/')
                break;
        }
        int len = dsize - i + strlen(leaf) + 1;
        char *name = (char *)malloc(sizeof(char) * (len));
        memset(name, '\0', (len));
        i++;
        int i2 = 0;
        for (; i2 < strlen(leaf); i2++)
        {
            name[i2] = leaf[i2];
        }

        for (; i < dsize; i++)
        {
            name[i2] = directory[i];
            i2++;
        }
        return name;
    }
    return "EXCEPTION";
}

/*
*   This function adds depth to the beginning of where it will be written according to the location of the file. 
*/
void addleaf(char **leaf)
{
    if ((*leaf) != NULL)
    {
        int len = strlen((*leaf)) + 1;
        char *buff = (char *)malloc(sizeof(char) * len + 1);
        memset(buff, '\0', (len + 1));
        strcpy(buff, *leaf);
        strcat(buff, "-");
        freePtr(*leaf);
        *leaf = buff;
    }
}

/*
*   This function changes the filename at the end of the directory to traverse
*/
void replace(char **directory, char *extension)
{

    if (*directory != NULL && extension != NULL)
    {

        int i = strlen(*directory);

        for (; i > -1; i--)
        {
            if ((*directory)[i] == '/')
                break;
        }

        int sizeDir = strlen(extension) + i + 1;

        char *str2 = (char *)malloc(sizeof(char) * sizeDir + 1);
        memset(str2, '\0', (sizeDir + 1));
        strncpy(str2, *directory, i);
        strcat(str2, "/");

        free(*directory);
        strcat(str2, extension);

        *directory = str2;
    }
}

/*
*   This function adds file name for to end of the directory for traverse
*/
void append(char **source, char *extension)
{

    int len = (strlen(extension) + strlen(*source) + 2);
    char *buff = (char *)malloc(sizeof(char) * (len));
    memset(buff, '\0', (len));
    if (buff != NULL && *source != NULL && extension != NULL)
    {
        strcpy(buff, *source);
        strcat(buff, "/");
        strcat(buff, extension);
    }
    if (*source != NULL)
    {
        free(*source);
    }
    *source = buff;
}
