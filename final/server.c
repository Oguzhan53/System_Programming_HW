
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <semaphore.h>

#define COLUMN_SIZE 256 /* Maximum column size */
#define QUERY_SIZE 2048 /* Maximum query size */
#define PORT_SIZE 10    /* Maximum port size */
#define NAME_LEN 64     /* Maximum name size*/

char *file_lock = "double_inst_lock"; /* Lock file name*/

/* Data base column node */
typedef struct node
{
    char **data;
    struct node *next;
    int size;

} node;

/* Connection queue node  */
typedef struct queue
{
    int s_fd;
    struct queue *next;
    struct queue *rear;

} queue;

queue *query_queue; /* Connection queue*/
queue *queue_tail;

node *data_base; /* Data base front*/
node *tail;      /* Data base tail */
int col_num = 0; /* Data base column number */

void lock(pthread_mutex_t *mutex);
void unlock(pthread_mutex_t *mutex);
void cwait(pthread_cond_t *cond, pthread_mutex_t *mutex);
void broadcast(pthread_cond_t *cond);
void p_sigal(pthread_cond_t *cond);

void free_res_row(node *row);
void free_res(node *res);
void free_src(void *source);
void error_exit();

int read_file(char *file_path);
void find_col_num(char *file_path);
void create_row(char buffer[][COLUMN_SIZE]);
node *nmalloc();
void print_db(node *head);
node *execute_query(char *query, int *len);
node *db_select(char columns[][COLUMN_SIZE], int size, int *len);
node *db_select_dist(char columns[][COLUMN_SIZE], int size, int *len);

node *add_row(node *tail, int size, char **buffer, int index[]);
int is_exist(node *res, int size, char **buffer, int index[]);
node *db_update(char columns[][COLUMN_SIZE], char upt_val[][COLUMN_SIZE], char cond_col[], char cond_val[], int size, int *len);
int dequeue();
void enqueue(int s_fd);
void empty_queue();

void print_log(char *str);

void create_lock();
void unlock_server();

void *thread_func(void *arg);
void free_resource();
void handler(int sig);

int becomeDaemon();

int socket1 = -1; /* Main thread connection descriptor */
int log_fd = -1;  /* Log file descriptor  */
int lock_fd = -1; /* Lock file decriptor */

int port = -1;       /* Port number */
int thread_num = -1; /* Thraed number */

pthread_t *thr_id = NULL; /* Thread id array */

/* For reader - writer paradigm  */
int AR = 0, AW = 0, WR = 0, WW = 0;
pthread_cond_t okToRead = PTHREAD_COND_INITIALIZER;
pthread_cond_t okToWrite = PTHREAD_COND_INITIALIZER;
pthread_mutex_t RWmutex = PTHREAD_MUTEX_INITIALIZER;

/* Writing log file mutex */
pthread_mutex_t Fmutex = PTHREAD_MUTEX_INITIALIZER;

/* Producer Consumner paradigm (For give connection descriptor to thread) */
int count = 0;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t full = PTHREAD_COND_INITIALIZER;
pthread_mutex_t PCmutex = PTHREAD_MUTEX_INITIALIZER;

sig_atomic_t end_flag = 0; /* End flag */

int main(int argc, char *argv[])
{

    signal(SIGINT, handler);

    char data_file[NAME_LEN];
    char log_file[NAME_LEN];
    memset(data_file, '\0', NAME_LEN);
    memset(log_file, '\0', NAME_LEN);

    int opt;
    while ((opt = getopt(argc, argv, "p:o:l:d:")) != -1)
    {
        switch (opt)
        {

        case 'p':

            if (optarg != NULL)
            {
                port = atoi(optarg);
            }

            break;
        case 'o':

            if (optarg != NULL)
            {
                if (strlen(optarg) > NAME_LEN)
                {
                    printf("Too long file path \n");
                    exit(EXIT_FAILURE);
                }
                strcpy(log_file, optarg);
            }
            break;
        case 'l':

            if (optarg != NULL)
            {

                thread_num = atoi(optarg);
            }
            break;
        case 'd':

            if (optarg != NULL)
            {
                if (strlen(optarg) > NAME_LEN)
                {
                    printf("Too long file path \n");
                    exit(EXIT_FAILURE);
                }
                strcpy(data_file, optarg);
            }
            break;

        case '?':
            fprintf(stderr, "Exiting...\n");
            exit(EXIT_FAILURE);
            break;
        }
    }
    if (port < 0 || strlen(log_file) == 0 || thread_num < 2 || strlen(data_file) == 0)
    {
        printf("Invalid input\nExiting...\n");
        unlock_server();
        exit(EXIT_FAILURE);
    }
    
    create_lock();
    if(becomeDaemon()==-1)
    {
        printf("Daemon server error\n");
        error_exit();
    }
    

    log_fd = open(log_file, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (log_fd == -1)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "Error open file : %s\n", strerror(errno));
        print_log(e_log);
        error_exit();
    }

    char log_buff[QUERY_SIZE];
    memset(log_buff, '\0', QUERY_SIZE);
    sprintf(log_buff, "-p %d", port);
    print_log(log_buff);
    sprintf(log_buff, "-o %s", log_file);
    print_log(log_buff);
    sprintf(log_buff, "-l %d", thread_num);
    print_log(log_buff);
    sprintf(log_buff, "-d %s", data_file);
    print_log(log_buff);
    sprintf(log_buff, "Loading dataset...");
    print_log(log_buff);

    clock_t begin = clock();

    int rec_num = read_file(data_file);

    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;

    sprintf(log_buff, "Dataset loaded in %.2f seconds with %d records.", time_spent, rec_num);
    print_log(log_buff);

    int s;
    if ((thr_id = (pthread_t *)malloc(sizeof(pthread_t) * thread_num)) == NULL)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "\nERROR:Out of memory.");
        print_log(e_log);
        error_exit();
    }
    int in[thread_num];
    for (size_t i = 0; i < thread_num; i++)
    {
        in[i] = i;
        s = pthread_create(&(thr_id[i]), NULL, thread_func, &in[i]);
        if (s != 0)
        {
            char e_log[QUERY_SIZE];
            memset(e_log, '\0', QUERY_SIZE);
            sprintf(e_log, "Error to create thread: %s\n", strerror(s));
            print_log(e_log);
            error_exit();
        }
    }

    struct sockaddr_in serverAddr;
    int socket2;
    int addrlen = sizeof(serverAddr);
    int opt_val = 1;

    if ((socket1 = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        error_exit();
    }

    if (setsockopt(socket1, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt_val, sizeof(opt_val)))
    {
        error_exit();
    }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(socket1, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        error_exit();
    }

    if (listen(socket1, 4096) < 0)
    {
        error_exit();
    }

    int j = 0;
    while (1)
    {
        if (socket1 != -2 && (socket2 = accept(socket1, (struct sockaddr *)&serverAddr, (socklen_t *)&addrlen)) < 0)
        {
            break;
        }
        if (end_flag)
            break;
        lock(&PCmutex);
        while (count == thread_num)
        {
            cwait(&empty, &PCmutex);

            if (end_flag)
                break;
        }
        if (end_flag)
        {
            broadcast(&full);
            unlock(&PCmutex);
            break;
        }
        enqueue(socket2);
        count++;
        broadcast(&full);
        unlock(&PCmutex);
        j++;
        if (end_flag)
            break;
    }

    for (size_t i = 0; i < thread_num; i++)
    {

        s = pthread_join(thr_id[i], NULL);
        if (s != 0)
        {
            char e_log[QUERY_SIZE];
            memset(e_log, '\0', QUERY_SIZE);
            sprintf(e_log, "Failed to join a thread: %s\n", strerror(s));
            print_log(e_log);
            error_exit();
        }
    }

    print_log("All threads have terminated, server shutting down.");
    free_resource();
    return 0;
}

/*
*   This function creates temporary file for prevent double instantiation to server
*/
void create_lock()
{

    lock_fd = open(file_lock, O_CREAT | O_EXCL);
    if (lock_fd == -1)
    {
        printf("It is not possible to start 2 instances of the server process\n");
        exit(EXIT_FAILURE);
    }
}

/*
*   This function removes temporary file
*/
void unlock_server()
{
   
        if (unlink(file_lock) == -1)
        {
            fprintf(stderr, "Error unlink temp file: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        close(lock_fd);
    
}

/*
*   This function make server to  daemon 
*/
int becomeDaemon()
{
    int maxfd, fd;

    switch (fork())
    {
    case -1:
        return -1;
    case 0:
        break;
    default:
        exit(EXIT_SUCCESS);
    }

    if (setsid() == -1)
        return -1;

    switch (fork())
    {
    case -1:
        return -1;
    case 0:
        break;
    default:
        exit(EXIT_SUCCESS);
    }

    umask(0);

    maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd == -1)
        maxfd = 8192;

    for (fd = 0; fd < maxfd; fd++)
        close(fd);

    return 0;
}

/*
*   This function prints long to file
*/
void print_log(char *str)
{
    pthread_mutex_lock(&Fmutex);

    time_t t; 
    time(&t);

    char log_data[QUERY_SIZE * 2];
    memset(log_data, '\0', QUERY_SIZE * 2);
    char buff[QUERY_SIZE];
    memset(buff, '\0', QUERY_SIZE);
    strcpy(buff, ctime(&t));
    int i = 0;
    while (buff[i] != '\n')
    {
        i++;
        if (buff[i] == '\0')
            break;
    }

    if (buff[i] == '\n')
        buff[i] = ']';

    sprintf(log_data, "[%s %s \n", buff, str);
    write(log_fd, log_data, strlen(log_data));
    pthread_mutex_unlock(&Fmutex);
}

/**
 *  This function empties the queue
*/
void empty_queue()
{
    while (1)
    {
        int s_fd = dequeue();
        if (s_fd < 0)
            break;
        close(s_fd);
    }
}

/**
 *  This function adds new element to queue
*/
void enqueue(int s_fd)
{

    if (query_queue == NULL)
    {
        query_queue = (queue *)malloc(sizeof(queue));
        queue_tail = query_queue;
        query_queue->s_fd = s_fd;
        query_queue->next = NULL;
        query_queue->rear = NULL;
    }
    else
    {
        queue *temp = (queue *)malloc(sizeof(queue));
        temp->s_fd = s_fd;
        temp->next = query_queue;
        temp->rear = NULL;
        query_queue->rear = temp;
        query_queue = temp;
    }
}

/**
 *  This function removes element from queue
*/
int dequeue()
{

    if (query_queue == NULL)
    {
        return -1;
    }
    else if (queue_tail == query_queue)
    {

        int fd = query_queue->s_fd;
        free_src(query_queue);
        query_queue = NULL;
        queue_tail = NULL;
        return fd;
    }
    else
    {

        int fd = queue_tail->s_fd;
        queue_tail = queue_tail->rear;
        free_src(queue_tail->next);
        return fd;
    }
}

/**
 *  This is thread function. It takes connection and reads and executes queries and send response to client
*/
void *thread_func(void *arg)
{
    int id = (*(int *)arg);
    char log_buf[QUERY_SIZE];
    memset(log_buf, '\0', QUERY_SIZE);
    char ch_id[PORT_SIZE];
    memset(ch_id, '\0', QUERY_SIZE);
    sprintf(ch_id, "%d", id);

    strcpy(log_buf, "Thread #");
    strcat(log_buf, ch_id);
    strcat(log_buf, ": waiting for connection");
    print_log(log_buf);
    int t_socket = -1;

    while (1)
    {
        node *res = NULL;
        if (end_flag)
        {
            break;
        }
        lock(&PCmutex);
        while (count == 0)
            cwait(&full, &PCmutex);
        t_socket = dequeue();
        memset(log_buf, '\0', QUERY_SIZE);
        sprintf(log_buf, "A connection has been delegated to thread id #%d", id);

        if (end_flag || t_socket == -1)
        {

            if (t_socket > 0)
            {
                close(t_socket);
            }
            free_res(res);
            broadcast(&empty);
            broadcast(&full);
            unlock(&PCmutex);
            break;
        }

        char query[QUERY_SIZE];
        int j = 0;
        while (1)
        {
            int len = 0;
            memset(query, '\0', QUERY_SIZE);

            if (read(t_socket, query, QUERY_SIZE) == -1)
            {
                char e_log[QUERY_SIZE];
                memset(e_log, '\0', QUERY_SIZE);
                sprintf(e_log, "Error to read socket\n");
                print_log(e_log);
                error_exit();
            }

            if (strcmp(query, "exit") == 0)
                break;

            if (query[0] == '\0')
                break;
            memset(log_buf, '\0', QUERY_SIZE);
            sprintf(log_buf, "Thread #%d: received query '%s'", id, query);
            print_log(log_buf);

            int in = 0;
            while (query[in] == ' ')
                in++;

            if (query[in] == 'S' || query[in] == 's')
            {

                lock(&RWmutex);
                while ((AW + WW) > 0)
                {
                    WR++;

                    cwait(&okToRead, &RWmutex);
                    WR--;
                }
                AR++;
                unlock(&RWmutex);

                res = execute_query(query, &len);

                lock(&RWmutex);
                AR--;
                if (AR == 0 && WW > 0)
                    p_sigal(&okToWrite);

                unlock(&RWmutex);
            }

            else
            {

                lock(&RWmutex);
                while ((AW + AR) > 0)
                {
                    WW++;

                    cwait(&okToWrite, &RWmutex);
                    WW--;
                }
                AW++;

                unlock(&RWmutex);
                res = execute_query(query, &len);
                lock(&RWmutex);
                AW--;
                if (WW > 0)
                    p_sigal(&okToWrite);
                else if (WR > 0)
                    broadcast(&okToRead);
                unlock(&RWmutex);
            }

            memset(query, '\0', QUERY_SIZE);
            sprintf(query, "%d", len);
            if (write(t_socket, query, QUERY_SIZE) == -1)
            {
                char e_log[QUERY_SIZE];
                memset(e_log, '\0', QUERY_SIZE);
                sprintf(e_log, "Error to write socket\n");
                print_log(e_log);
                error_exit();
            }

            node *iter = res;
            for (size_t i = 0; i < len; i++)
            {

                memset(query, '\0', QUERY_SIZE);
                for (size_t i2 = 0; i2 < iter->size; i2++)
                {
                    if (i2 == 0)
                        strcpy(query, iter->data[i2]);
                    else
                    {
                        strcat(query, iter->data[i2]);
                    }
                    strcat(query, ",\t\t");
                }
                strcat(query, "\n");
                if (write(t_socket, query, QUERY_SIZE) == -1)
                {
                    char e_log[QUERY_SIZE];
                    memset(e_log, '\0', QUERY_SIZE);
                    sprintf(e_log, "Error to write socket\n");
                    print_log(e_log);
                    error_exit();
                }
                iter = iter->next;
            }

            memset(log_buf, '\0', QUERY_SIZE);
            sprintf(log_buf, "Thread #%d: query completed, %d records have been returned.", id, len);
            print_log(log_buf);
            free_res(res);
            j++;
            usleep(500);
        }
        if (t_socket > 0)
            close(t_socket);

        count--;

        broadcast(&empty);
        unlock(&PCmutex);

        if (end_flag)
            break;
    }

    return NULL;
}

/**
 *  This function executes the update queries 
*/
node *db_update(char columns[][COLUMN_SIZE], char upt_val[][COLUMN_SIZE], char cond_col[], char cond_val[], int size, int *len)
{

    if (data_base == NULL)
        return NULL;
    int index[size];
    int i3 = 0;
    int cond_in = -1;

    for (size_t i = 0; i < size; i++)
    {
        index[i] = 0;
        for (size_t i2 = 0; i2 < col_num; i2++)
        {
            if (strcasecmp(columns[i], data_base->data[i2]) == 0)
            {

                index[i3] = i2;
                i3++;
            }
            if (strcasecmp(cond_col, data_base->data[i2]) == 0)
            {

                cond_in = i2;
            }
        }
    }
    if (i3 != size || cond_in == -1)
        return NULL;

    int fl = 1;
    node *result = NULL;
    node *r_tail;
    node *iter = data_base;

    int index2[data_base->size];
    for (size_t i = 0; i < data_base->size; i++)
    {
        index2[i] = i;
    }

    while (iter != NULL)
    {

        if (strcasecmp(cond_val, iter->data[cond_in]) == 0)
        {
            *len = *len + 1;
            for (size_t i = 0; i < size; i++)
            {
                memset(iter->data[index[i]], '\0', COLUMN_SIZE);
                strcpy(iter->data[index[i]], upt_val[i]);
            }
            if (fl)
            {
                result = add_row(result, iter->size, iter->data, index2);
                r_tail = result;
                fl = 0;
            }
            else
            {
                r_tail = add_row(r_tail, iter->size, iter->data, index2);
            }
        }

        iter = iter->next;
    }

    return result;
}

/**
 *  This function executes the select distinct queries 
*/
node *db_select_dist(char columns[][COLUMN_SIZE], int size, int *len)
{
    if (data_base == NULL)
        return NULL;
    int index[size];
    int i3 = 0;
    for (size_t i = 0; i < size; i++)
    {
        index[i] = 0;
        for (size_t i2 = 0; i2 < col_num; i2++)
        {
            if (strcasecmp(columns[i], data_base->data[i2]) == 0)
            {
                index[i3] = i2;
                i3++;
            }
        }
    }
    if (i3 != size)
        return NULL;

    node *iter;
    node *result = NULL;
    node *r_tail;
    iter = data_base;
    int fl = 1;

    *len = 0;
    while (iter != NULL)
    {

        if (fl)
        {
            *len = *len + 1;
            result = add_row(result, size, iter->data, index);
            r_tail = result;
            fl = 0;
        }
        else
        {
            if (!is_exist(result, size, iter->data, index))
            {
                *len = *len + 1;
                r_tail = add_row(r_tail, size, iter->data, index);
            }
        }

        iter = iter->next;
    }

    return result;
}

/**
 *  This function adds new row to database or queue
*/
node *add_row(node *tail, int size, char **buffer, int index[])
{
    if (tail == NULL)
    {

        tail = nmalloc();
    }
    else
    {

        tail->next = nmalloc();
        tail = tail->next;
    }

    if ((tail->data = (char **)malloc(size * sizeof(char *))) == NULL)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "\nERROR:Out of memory.");
        print_log(e_log);
        error_exit();
    }
    for (size_t i = 0; i < size; i++)
    {
        if ((tail->data[i] = (char *)malloc(COLUMN_SIZE * sizeof(char))) == NULL)
        {
            char e_log[QUERY_SIZE];
            memset(e_log, '\0', QUERY_SIZE);
            sprintf(e_log, "\nERROR:Out of memory.");
            print_log(e_log);
            error_exit();
        }
        memset(tail->data[i], '\0', COLUMN_SIZE);
        strcpy(tail->data[i], buffer[index[i]]);
    }

    tail->size = size;
    tail->next = NULL;
    return tail;
}

/**
 *  This function used for select distinct queries. It checks the record already taken or not
*/
int is_exist(node *res, int size, char **buffer, int index[])
{

    node *iter;
    if (res->next != NULL)
        iter = res->next;
    else
        iter = res;
    int fl;
    while (iter != NULL)
    {
        fl = 1;
        for (size_t i = 0; i < size; i++)
        {

            if (strcasecmp(buffer[index[i]], iter->data[i]) != 0)
                fl = 0;
        }
        if (fl)
            return fl;
        iter = iter->next;
    }
    return 0;
}

/**
 *  This function executes the select queries 
*/
node *db_select(char columns[][COLUMN_SIZE], int size, int *len)
{

    if (data_base == NULL)
        return NULL;

    if (size == -1)
        size = data_base->size;

    int index[size];
    if (columns == NULL)
        for (size_t i = 0; i < size; i++)
            index[i] = i;
    else
    {
        int i3 = 0;
        for (size_t i = 0; i < size; i++)
        {
            index[i] = 0;
            for (size_t i2 = 0; i2 < col_num; i2++)
            {
                if (strcasecmp(columns[i], data_base->data[i2]) == 0)
                {
                    index[i3] = i2;
                    i3++;
                }
            }
        }
        if (i3 == 0)
            return NULL;
    }

    node *res = NULL;
    node *r_tail = res;
    node *iter;
    iter = data_base;
    int fl = 1;
    while (iter != NULL)
    {
        *len = *len + 1;
        if (fl)
        {
            res = add_row(res, size, iter->data, index);
            r_tail = res;
            fl = 0;
        }
        else
            r_tail = add_row(r_tail, size, iter->data, index);

        iter = iter->next;
    }

    return res;
}

/**
 *  This function locks the mutex
*/
void lock(pthread_mutex_t *mutex)
{

    int s = pthread_mutex_lock(mutex);
    if (s != 0)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "Error to lock mutex: %s\n", strerror(s));
        print_log(e_log);
        error_exit();
    }
}

/**
 *  This function unlocks the mutex
*/
void unlock(pthread_mutex_t *mutex)
{
    int s = pthread_mutex_unlock(mutex);
    if (s != 0)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "Error to lock mutex: %s\n", strerror(s));
        print_log(e_log);
        error_exit();
    }
}

/**
 *  This function waits the condition wariable
*/
void cwait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{

    int s = pthread_cond_wait(cond, mutex);
    if (s != 0)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "Error to wait cond. var.: %s\n", strerror(s));
        print_log(e_log);
        error_exit();
    }
}

/**
 *  This function awakes the threads 
*/
void broadcast(pthread_cond_t *cond)
{
    int s = pthread_cond_broadcast(cond);
    if (s != 0)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "Error to broadcast cond. var.: %s\n", strerror(s));
        print_log(e_log);
        error_exit();
    }
}

/**
 *  This function awakes the thread 
*/
void p_sigal(pthread_cond_t *cond)
{
    int s = pthread_cond_signal(cond);
    if (s != 0)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "Error to signal cond. var.: %s\n", strerror(s));
        print_log(e_log);
        error_exit();
    }
}

/**
 *  This function executes the query
*/
node *execute_query(char *query, int *len)
{
    if (query == NULL)
        return NULL;

    int q_size = strlen(query);
    int i = 0;
    int comma = 0;       // comma number
    if (query[0] == ' ') // trim query
        while (query[i] == ' ')
            i++;
    node *res = NULL;
    char command[COLUMN_SIZE];
    memset(command, '\0', COLUMN_SIZE);
    if (query[i] == 'S' || query[i] == 's')
    {

        while (query[i] != ' ')
            i++;
        while (query[i] == ' ')
            i++;
        int j = 0, j1 = i;
        while (query[i] != ' ')
        {
            command[j] = query[i];
            i++;
            j++;
        }

        if (strcasecmp(command, "DISTINCT") == 0)
        {

            while (query[i] == ' ')
                i++;

            for (j = i; j < q_size; j++)
                if (query[j] == ',')
                    comma++;
            comma++;
            char column_name[comma][COLUMN_SIZE];
            for (size_t i2 = 0; i2 < comma; i2++)
                memset(column_name[i2], '\0', COLUMN_SIZE);
            int i3 = 0, i4 = 0;
            j = i;
            while (i3 < comma)
            {

                if (query[j] == ' ' || query[j] == ',')
                {
                    while (query[j] == ' ' || query[j] == ',')
                        j++;
                    j--;
                    i3++;
                    i4 = 0;
                }
                else
                {
                    column_name[i3][i4] = query[j];

                    i4++;
                }
                j++;
            }

            res = db_select_dist(column_name, comma, len);
        }
        else
        {
            int i3 = 0, i4 = 0;
            if (strcmp(command, "*") == 0)
                res = db_select(NULL, -1, len);
            else
            {

                i = j1;
                while (query[i] == ' ')
                    i++;
                for (j = i; j < q_size; j++)
                    if (query[j] == ',')
                        comma++;
                comma++;
                char column_name[comma][COLUMN_SIZE];
                for (size_t i2 = 0; i2 < comma; i2++)
                    memset(column_name[i2], '\0', COLUMN_SIZE);
                j = i;
                while (i3 < comma)
                {

                    if (query[j] == ' ' || query[j] == ',')
                    {
                        while (query[j] == ' ' || query[j] == ',')
                            j++;
                        j--;
                        i3++;
                        i4 = 0;
                    }
                    else
                    {
                        column_name[i3][i4] = query[j];

                        i4++;
                    }
                    j++;
                }

                res = db_select(column_name, comma, len);
            }
        }
    }
    else
    {

        int j = 0;
        int comma = 0;
        while (query[i] != ' ')
        {
            command[j] = query[i];
            i++;
            j++;
        }
        if (strcasecmp(command, "update") != 0)
            return NULL;
        memset(command, '\0', COLUMN_SIZE);
        while (query[i] == ' ')
            i++;
        j = 0;
        while (query[i] != ' ')
        {
            command[j] = query[i];
            i++;
            j++;
        }
        if (strcasecmp(command, "table") != 0)
            return NULL;
        while (query[i] == ' ')
            i++;
        memset(command, '\0', COLUMN_SIZE);
        j = 0;
        while (query[i] != ' ')
        {
            command[j] = query[i];
            i++;
            j++;
        }
        if (strcasecmp(command, "set") != 0)
            return NULL;

        while (query[i] == ' ')
            i++;

        for (j = i; j < q_size; j++)
            if (query[j] == ',')
                comma++;
        comma++;
        char condition[2][COLUMN_SIZE];
        for (size_t i2 = 0; i2 < 2; i2++)
            memset(condition[i2], '\0', COLUMN_SIZE);
        char column_name[comma][COLUMN_SIZE];
        char column_val[comma][COLUMN_SIZE];
        for (size_t i2 = 0; i2 < comma; i2++)
        {
            memset(column_name[i2], '\0', COLUMN_SIZE);
            memset(column_val[i2], '\0', COLUMN_SIZE);
        }

        int i3 = 0, i4 = 0, i5 = 0, i6 = 0, fl4 = 1;
        j = i;
        while (i3 < comma)
        {

            if ((query[j] == ' ' || query[j] == '=') && fl4)
            {
                fl4 = 0;
                while (query[j] != '\'')
                    j++;

                i4 = 0;
            }
            else if ((query[j] == '\'') && !fl4)
            {
                fl4 = 1;
                j++;
                while (query[j] == ' ' || query[j] == ',')
                    j++;
                j--;
                i3++;
                i6 = 0;
                i5++;
            }
            else if (fl4)
            {
                column_name[i3][i4] = query[j];

                i4++;
            }
            else
            {
                column_val[i5][i6] = query[j];

                i6++;
            }

            j++;
        }
        i = j;
        char chc_col_name[COLUMN_SIZE];
        char upt_val[COLUMN_SIZE];
        memset(chc_col_name, '\0', COLUMN_SIZE);
        memset(upt_val, '\0', COLUMN_SIZE);
        i3 = 0, i4 = 0, i5 = 0;
        memset(command, '\0', COLUMN_SIZE);
        while (query[i] != ' ')
        {
            command[i3] = query[i];
            i3++;
            i++;
        }
        if (strcasecmp(command, "where") != 0)
        {
            print_log("Invalid SQL Command Error\n");
            return NULL;
        }
        while (query[i] == ' ')
            i++;
        i3 = 0;
        while (query[i] != ' ' && query[i] != '=')
        {
            chc_col_name[i3] = query[i];
            i++;
            i3++;
        }
        while (query[i] != '\'')
            i++;
        i++;
        i3 = 0;
        while (query[i] != '\'' && i < q_size)
        {
            upt_val[i3] = query[i];
            i++;
            i3++;
        }
        res = db_update(column_name, column_val, chc_col_name, upt_val, comma, len);
    }

    return res;
}

/**
 *  This function prints the database (for test)
*/
void print_db(node *head)
{
    if (head == NULL)
        return;
    node *iter = head;
    while (iter != NULL)
    {
        for (size_t i = 0; i < iter->size; i++)
        {
            printf("%s\t", iter->data[i]);
        }
        printf("\n");
        iter = iter->next;
    }

    printf("\n");
}

/**
 *  This function create and add new rows to database
*/
void create_row(char buffer[][COLUMN_SIZE])
{
    if (data_base == NULL)
    {
        data_base = nmalloc();
        tail = data_base;
    }
    else
    {
        tail->next = nmalloc();
        tail = tail->next;
    }

    if ((tail->data = (char **)malloc(col_num * sizeof(char *))) == NULL)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "\nERROR:Out of memory.");
        print_log(e_log);
        error_exit();
    }
    for (size_t i = 0; i < col_num; i++)
    {
        if ((tail->data[i] = (char *)malloc(COLUMN_SIZE * sizeof(char))) == NULL)
        {
            char e_log[QUERY_SIZE];
            memset(e_log, '\0', QUERY_SIZE);
            sprintf(e_log, "\nERROR:Out of memory.");
            print_log(e_log);
            error_exit();
        }
        memset(tail->data[i], '\0', COLUMN_SIZE);
        strcpy(tail->data[i], buffer[i]);
    }
    tail->size = col_num;
    tail->next = NULL;
}

/**
 *  This function take place for node
*/
node *nmalloc()
{
    node *p = NULL;
    if ((p = (node *)malloc(sizeof(node))) == NULL)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "Out of memory.\n");
        print_log(e_log);
        error_exit();
    }
    p->next = NULL;
    p->size = 0;
    p->data = NULL;
    return p;
}

/*
*   This function reads the database file.
*/
int read_file(char *file_path)
{
    find_col_num(file_path);
    int rec_num = 0;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
    int fd = open(file_path, O_RDONLY, mode);
    if (fd == -1)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "Error open file : %s\n", strerror(errno));
        print_log(e_log);
        error_exit();
    }
    size_t bytes_read, offset = 0;
    char c[1];
    memset(c, '\0', 1);
    off_t file_length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    int fl = 1; 
    char buffer[col_num][COLUMN_SIZE];
    int j = 0, j1 = 0;
    for (size_t i = 0; i < col_num; i++)
        memset(buffer[i], '\0', COLUMN_SIZE);
    do
    {

        bytes_read = read(fd, c, 1);

        offset += bytes_read;
        if (fl && c[0] == '\"') // if the ' operatetor start
            fl = 0;

        else if (!fl && c[0] == '\"') // if the ' operator end
            fl = 1;

        else if (fl && c[0] == ',')
        {
            j++;
            j1 = 0;
        }
        else if (fl && c[0] == '\n') // count the coulm number
        {

            rec_num++;
            create_row(buffer);
            for (size_t i = 0; i < col_num; i++)
            {
                memset(buffer[i], '\0', COLUMN_SIZE);
            }
            j1 = 0;
            j = 0;
        }
        else
        {
            buffer[j][j1] = c[0];
            j1++;
        }

    } while (offset < file_length);

    close(fd);
    return rec_num;
}

/*
*   This function finds the row number in the database
*/
void find_col_num(char *file_path)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
    int fd = open(file_path, O_RDONLY, mode);
    if (fd == -1)
    {
        char e_log[QUERY_SIZE];
        memset(e_log, '\0', QUERY_SIZE);
        sprintf(e_log, "Error open file : %s\n", strerror(errno));
        print_log(e_log);
        error_exit();
    }
    size_t bytes_read, offset = 0;
    char c[1];

    memset(c, '\0', 1);
    off_t file_length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    int fl = 1; 
    do
    {
        bytes_read = read(fd, c, 1);

        offset += bytes_read;
        if (fl && c[0] == '\"') // if the ' operatetor start
            fl = 0;
        else if (!fl && c[0] == '\"') // if the ' operator end
            fl = 1;
        else if (fl && c[0] == ',') // count the coulm number
            col_num++;
        else if (fl && c[0] == '\n') // if the first row end then create db
        {
            col_num++;
            break;
        }

    } while (offset < file_length);

    close(fd);
}

/*
*   This function frees a pointer
*/
void free_src(void *source)
{
    if (source == NULL)
        return;
    free(source);
}

/**
 *  This function frees the row nodes
*/
void free_res_row(node *row)
{
    for (size_t i = 0; i < row->size; i++)
    {
        free_src(row->data[i]);
    }
    free_src(row->data);
    free_src(row);
}

/**
 *  This function frees the database and query response
*/
void free_res(node *res)
{
    if (res == NULL)
        return;
    node *iter = res;
    while (iter != NULL)
    {
        node *temp;
        temp = iter;
        iter = iter->next;
        free_res_row(temp);
    }
}

/**
 *  Error exit function
*/
void error_exit()
{
    free_resource();
    exit(EXIT_FAILURE);
}

/**
 *  This function frees all global resources
*/
void free_resource()
{
    if (socket1 > 0)
    {
        shutdown(socket1, SHUT_RD);
        close(socket1);
    }
    socket1 = -2;
    if (log_fd > 0)
        close(log_fd);
    free_res(data_base);
    empty_queue();
    free_src(thr_id);
    unlock_server();
}
/**
 *  This is SIGINT signal handler
*/
void handler(int sig)
{

    int savedErrno = errno;
    print_log("Termination signal received, waiting for ongoing threads to complete.");
    if (sig == SIGINT)
    {

        if (socket1 > 0)
        {
            shutdown(socket1, SHUT_RD);
            close(socket1);
        }
        socket1 = -2;
        end_flag = 1;
        count++;
        broadcast(&empty);
        broadcast(&full);
    }

    errno = savedErrno;

    return;
}