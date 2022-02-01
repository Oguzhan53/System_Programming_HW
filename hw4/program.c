#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/file.h>
#include <signal.h>
#include <sys/wait.h>

#define FILEPATHSIZE 1024 /* Max file path size */
#define MAX_S_Q 5         /* Maximum value of speed and quality */
#define MIN_S_Q 1         /* Minimum value of speed and quality */
#define MAX_C 1000        /* Maximum value of cost */
#define MIN_C 100         /* Minimum value of cost */
#define ATT_NUM 3         /* Attribute number */
#define NAME_LEN 256      /* Maximum name legth */
#define NUM_LEN 64        /* Maximum number lengh */
#define SL_TIME 6         /* Sleep time */
#define R 0               /* Reading end of pipes */
#define W 1               /* Writing end of pipes */
typedef struct            /* This struct keeps student information and gives the related thread */
{
    char name[NAME_LEN];
    int index; /* Attribute arrays index */
    int speed;
    int cost;
    int quality;
} students;
typedef enum /* Attributes enum */
{
    Q,
    S,
    C,
    Empty
} attribute;

typedef struct node /* Homework queue node */
{
    attribute hw;
    struct node *next;

} node;

/* Global variables */
pthread_t ch_id;       /* Cheater thread id */
pthread_t *st_id;      /* Hired students thread ids */
sem_t *start;          /* Starter semaphore , wait main thread for initialize student thread */
node *hw_queue = NULL; /* Homework queue */
sem_t *queue_per;      /* Semaphore for access to homework queue*/
sem_t *full;           /* Semaphore for take homework from queue (determines if there is data in queue) */
int *busy_st;          /* Busy student array , if student is busy then her index value will be 1 */
int **stu_pipes;       /* Hired studnts pipes */
int money;             /* Cheater money */
sem_t *empty;          /* Semaphore for wait avaliable student */
sem_t *resource;       /* Semaphore for access the resource */
int is_end;            /* Homework end flag */
int st_num;            /* Hired student number */
int min_cost;          /* Minimum cost of done homework */
sig_atomic_t exit_fl;  /* Exit flag */

void *cheater(void *arg);
void *st_for_hire(void *arg);
void enqueue(attribute hw);
void read_students(char *file_path, char names[][NAME_LEN], int attributes[][ATT_NUM], int *min_cost);
int find_st_num(char *file_path);
attribute dequeue();
void wait_sem(sem_t *sem);
void post_sem(sem_t *sem);
void destroy_sem(sem_t *sem);
void init_sem(sem_t *sem, int val);
void close_read_pipes(int **pipes, int size);
int open_pipes(int **pipes, int size);
void close_pipe(int *pipe, int end);
void close_write_pipes(int **pipes, int size);
int find_appropriate_stu(attribute att, int st_num, int attributes[][ATT_NUM], int *money);
void free_resource();
void free_src(void *source);
void error_exit();
void handler(int signal);
void empty_queue();
int check_hw_file(char *file_path);
int main(int argc, char *argv[])
{
    int s; /* for pthread operations error */
    char stud_file[FILEPATHSIZE];
    char hw_file[FILEPATHSIZE];
    memset(stud_file, '\0', FILEPATHSIZE);
    memset(hw_file, '\0', FILEPATHSIZE);
    min_cost = 0;
    is_end = 0;
    if (argc != 4)
    {
        printf("Invalid input parameter\nExiting...\n");
        exit(EXIT_FAILURE);
    }
    strcpy(hw_file, argv[1]);
    strcpy(stud_file, argv[2]);
    money = atoi(argv[3]);
    if (money < MIN_C)
    {
        printf("Invalid money. Monet must be =>100\nExiting...\n");
        exit(EXIT_FAILURE);
    }

    if (check_hw_file(hw_file) == -1)
    {
        printf("Invalid homework file content.\nExiting...\n");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handler;
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("Failed to install SIGINT signal handler");
        error_exit();
    }

    if ((start = (sem_t *)malloc(sizeof(sem_t))) == NULL)
    {
        fprintf(stderr, "\nERROR:Out of memory.");
        error_exit();
    }
    if ((queue_per = (sem_t *)malloc(sizeof(sem_t))) == NULL)
    {
        fprintf(stderr, "\nERROR:Out of memory.");
        error_exit();
    }
    if ((full = (sem_t *)malloc(sizeof(sem_t))) == NULL)
    {
        fprintf(stderr, "\nERROR:Out of memory.");
        error_exit();
    }
    if ((empty = (sem_t *)malloc(sizeof(sem_t))) == NULL)
    {
        fprintf(stderr, "\nERROR:Out of memory.");
        error_exit();
    }
    if ((resource = (sem_t *)malloc(sizeof(sem_t))) == NULL)
    {
        fprintf(stderr, "\nERROR:Out of memory.");
        error_exit();
    }

    st_num = find_st_num(stud_file);
    int maden_hw[st_num];

    if ((busy_st = (int *)malloc(sizeof(int) * st_num)) == NULL)
    {
        fprintf(stderr, "\nERROR:Out of memory.");
        error_exit();
    }
    char names[st_num][NAME_LEN];
    for (size_t i = 0; i < st_num; i++)
    {
        busy_st[i] = 0;
        maden_hw[i] = 0;
        memset(names[i], '\0', NAME_LEN);
    }

    int attributes[st_num][ATT_NUM];

    read_students(stud_file, names, attributes, &min_cost);

    students stu[st_num];

    if ((st_id = (pthread_t *)malloc(sizeof(pthread_t) * st_num)) == NULL)
    {
        fprintf(stderr, "\nERROR:Out of memory.");
        error_exit();
    }
    for (size_t i = 0; i < st_num; i++)
    {
        st_id[i] = -1;
    }
    ch_id = -1;

    if ((stu_pipes = (int **)malloc(st_num * sizeof(int *))) == NULL)
    {
        fprintf(stderr, "\nERROR:Out of memory.");
        error_exit();
    }
    for (size_t i = 0; i < st_num; i++)
        if ((stu_pipes[i] = (int *)malloc(2 * sizeof(int))) == NULL)
        {
            fprintf(stderr, "\nERROR:Out of memory.");
            error_exit();
        }

    open_pipes(stu_pipes, st_num);

    for (size_t i = 0; i < st_num; i++)
    {
        memset(stu[i].name, '\0', NAME_LEN);
        memcpy(stu[i].name, names[i], strlen(names[i]));
        stu[i].quality = attributes[i][0];
        stu[i].speed = attributes[i][1];
        stu[i].cost = attributes[i][2];
        stu[i].index = i;
    }

    init_sem(start, 1);
    init_sem(queue_per, 1);
    init_sem(full, 0);
    init_sem(empty, st_num);
    init_sem(resource, 1);
    pthread_t temp;
    for (size_t i = 0; i < st_num; i++)
    {

        wait_sem(start);

        s = pthread_create(&temp, NULL, st_for_hire, &stu[i]);
        if (s != 0)
        {
            fprintf(stderr, "Error to create thread: %s\n", strerror(s));
            error_exit();
        }
        st_id[i] = temp;
    }
    s = pthread_create(&temp, NULL, cheater, (void *)hw_file);
    if (s != 0)
    {
        fprintf(stderr, "Error to create thread: %s\n", strerror(s));
        error_exit();
    }
    ch_id = temp;

    /*---------------------MAIN AREA ---------------*/
    attribute att;
    int end_money;
    while (1)
    {

        wait_sem(resource);
        if (min_cost > money) /* If money is end */
        {
            att = Empty;
            for (size_t i = 0; i < st_num; i++)
            {
                if (write(stu_pipes[i][W], &att, sizeof(att)) != sizeof(att))
                {
                    fprintf(stderr, "Error write pipe: %s\n", strerror(errno));
                    error_exit();
                }
            }
            end_money = 1;
            post_sem(resource);
            break;
        }
        post_sem(resource);

        wait_sem(full);
        wait_sem(queue_per);
        att = dequeue();
        if (att == Empty && is_end) /* if homeworks end */
        {
            for (size_t i2 = 0; i2 < st_num; i2++)
            {

                if (write(stu_pipes[i2][W], &att, sizeof(att)) != sizeof(att))
                {
                    fprintf(stderr, "Error write pipe: %s\n", strerror(errno));
                    error_exit();
                }
            }

            post_sem(queue_per);
            end_money = 0;

            break;
        }
        post_sem(queue_per);

        for (int i3 = 0; i3 < st_num; i3++)
        {
            wait_sem(empty);
            wait_sem(resource);
            int st = find_appropriate_stu(att, st_num, attributes, &money);
            if (st == -1)
                post_sem(resource);

            else
            {
                post_sem(resource);
                maden_hw[st] += 1;
                if (write(stu_pipes[st][W], &att, sizeof(att)) != sizeof(att))
                {
                    fprintf(stderr, "Error write pipe: %s\n", strerror(errno));
                    error_exit();
                }
                break;
            }
        }
    }

    /*--------------------------------------*/

    s = pthread_join(ch_id, NULL);
    if (s != 0)
    {
        fprintf(stderr, "Failed to join a thread: %s\n", strerror(s));
        error_exit();
    }

    for (size_t i = 0; i < st_num; i++)
    {

        s = pthread_join(st_id[i], NULL);
        if (s != 0)
        {
            fprintf(stderr, "Failed to join a thread: %s\n", strerror(s));
            error_exit();
        }
    }
    printf("---------------------------\n");
    if (end_money)
        printf("Money is over, closing.\n");
    else
        printf("No more homeworks left or coming in, closing.\n");

    printf("Homeworks solved and money made by the students:\n");
    int total = 0;
    int hw_num = 0;
    for (size_t i = 0; i < st_num; i++)
    {
        printf("%s %d %d\n", stu[i].name, maden_hw[i], stu[i].cost * maden_hw[i]);
        total += stu[i].cost * maden_hw[i];
        hw_num += maden_hw[i];
    }

    printf("Total cost for %d homeworks %d TL\n", hw_num, total);
    printf("Money left at G's account: %d TL\n", money);

    free_resource();
    empty_queue();
    printf("Program Terminated \n");

    exit(EXIT_SUCCESS);
}

/*
*   This function checks the homework file content before start giving hired students.
*/
int check_hw_file(char *file_path)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
    int fd = open(file_path, O_RDONLY, mode);
    if (fd == -1)
    {
        fprintf(stderr, "Error open file : %s\n", strerror(errno));
        error_exit();
    }
    size_t bytes_read, offset = 0;
    char c[1];
    memset(c, '\0', 1);
    off_t file_length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    do
    {
        bytes_read = read(fd, c, 1);
        offset += bytes_read;
        if (c[0] == 'S' || c[0] == 'Q' || c[0] == 'C' || c[0] == ' ' || c[0] == '\n')
            continue;
        return -1;

    } while (offset < file_length);

    return 0;
}

/*
*   This function empties the queue.
*/
void empty_queue()
{
    while (1)
        if (dequeue() == Empty)
            break;
}

/*
*   This function frees the all global resources
*/
void free_resource()
{
    close_write_pipes(stu_pipes, st_num);
    close_read_pipes(stu_pipes, st_num);
    destroy_sem(start);
    destroy_sem(queue_per);
    destroy_sem(full);
    destroy_sem(resource);
    free_src(resource);
    free_src(start);
    free_src(queue_per);
    free_src(full);
    free_src(empty);
    free_src(busy_st);
    free_src(st_id);
    if (stu_pipes != NULL)
        for (size_t i = 0; i < st_num; i++)
            free_src(stu_pipes[i]);
    free_src(stu_pipes);
    empty_queue();
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

/*
*   This function finds the appropriate student for current homework
*/
int find_appropriate_stu(attribute att, int st_num, int attributes[][ATT_NUM], int *money)
{
    int fl = 0;
    int st_in = 0;
    for (size_t i = 0; i < st_num; i++)
    {
        if (!busy_st[i] && *money >= attributes[i][C])
        {
            st_in = i;
            fl = 1;
            break;
        }
    }

    int i;
    fl = 0;
    switch (att)
    {
    case C: // if priority is cost then choose lowest cost

        for (i = 0; i < st_num; i++)
        {
            if ((attributes[i][att] <= attributes[st_in][att]) && !(busy_st[i]) && *money >= attributes[i][att])
            {
                st_in = i;
                fl = 1;
            }
        }
        if (fl)
        {
            busy_st[st_in] = 1;
            *money -= attributes[st_in][att];
            return st_in;
        }
        break;

    default: // if priority is speed or quality then choose highest value of this attributes

        for (i = 0; i < st_num; i++)
        {
            //printf("att : %d \n",attributes[i][att]);
            if ((attributes[i][att] >= attributes[st_in][att]) && *money >= attributes[i][C] && !(busy_st[i]))
            {
                fl = 1;
                st_in = i;
            }
        }
        if (fl)
        {
            busy_st[st_in] = 1;
            *money -= attributes[st_in][C];
            return st_in;
        }

        break;
    }

    return -1;
}

/*
*   This function opens pipes
*/
int open_pipes(int **pipes, int size)
{
    if (pipes == NULL)
        return -1;
    for (size_t i = 0; i < size; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            fprintf(stderr, "Error open  pipe : %s\n", strerror(errno));
            error_exit();
        }
    }

    return 0;
}
/*
*   This funtion closes read end of the pipes
*/
void close_read_pipes(int **pipes, int size)
{
    if (pipes == NULL)
        return;
    for (size_t i = 0; i < size; i++)
    {

        if (close(pipes[i][R]) == -1)
        {
            fprintf(stderr, "Error closing reading end of pipe : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
}
/*
*   This function closes write end of the pipes
*/
void close_write_pipes(int **pipes, int size)
{
    if (pipes == NULL)
        return;
    for (size_t i = 0; i < size; i++)
    {

        if (close(pipes[i][W]) == -1)
        {
            fprintf(stderr, "Error closing reading end of pipe : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
}

/*
*   This fuction closes pipe
*/
void close_pipe(int *pipe, int end)
{
    if (pipe == NULL)
        return;
    if (close(pipe[end]) == -1)
    {
        fprintf(stderr, "Error closing reading end of pipe : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/*
*   This function initializes semaphore with a value
*/
void init_sem(sem_t *sem, int val)
{
    if (sem_init(sem, 0, val) == -1)
    {
        fprintf(stderr, "Error initialize clinic semaphore: %s\n", strerror(errno));
        error_exit();
    }
}

/*
*   This function waits the semaphore
*/
void wait_sem(sem_t *sem)
{
    if (sem_wait(sem) == -1)
    {
        fprintf(stderr, "Error wait semaphore: %s\n", strerror(errno));
        error_exit();
    }
}

/*
*   This function posts the semaphore
*/
void post_sem(sem_t *sem)
{
    if (sem_post(sem) == -1)
    {
        fprintf(stderr, "Error post semaphore : %s\n", strerror(errno));
        error_exit();
    }
}

/*
*   This function destroys the semaphore
*/
void destroy_sem(sem_t *sem)
{
    if (sem == NULL)
        return;
    if (sem_destroy(sem) == -1)
    {
        fprintf(stderr, "Error destroy semaphore: %s\n", strerror(errno));
        error_exit();
    }
}

/*
*   This function enqueues the homework queue
*/
void enqueue(attribute hw)
{
    node *new_hw;
    if ((new_hw = (node *)malloc(sizeof(node))) == NULL)
    {
        fprintf(stderr, "\nERROR:Out of memory.");
        error_exit();
    }
    new_hw->hw = hw;
    if (hw_queue == NULL)
        new_hw->next = new_hw;
    else
    {
        new_hw->next = hw_queue->next;
        hw_queue->next = new_hw;
    }
    hw_queue = new_hw;
}

/*
*   This function denqueues the homework queue
*/
attribute dequeue()
{
    if (!hw_queue)
        return Empty;
    node *iter;
    iter = hw_queue->next;
    attribute hw = iter->hw;

    if (iter == hw_queue)
        hw_queue = NULL;
    else
        hw_queue->next = iter->next;
    free(iter);
    return hw;
}

/*
*   This is cheater thread function
*/
void *cheater(void *arg)
{
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ENABLE, NULL);
    char *file_path = (char *)arg;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
    int fd = open(file_path, O_RDONLY, mode);
    if (fd == -1)
    {
        fprintf(stderr, "Error open file : %s\n", strerror(errno));
        error_exit();
    }
    size_t bytes_read, offset = 0;
    char c[1];
    memset(c, '\0', 1);
    off_t file_length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    int i1 = 0;
    int is_money_end = 0;
    int curr_money;
    do
    {

        wait_sem(resource);

        if (money < min_cost)
        {
            is_money_end = 1;
            post_sem(resource);
            break;
        }
        curr_money = money;
        post_sem(resource);
        bytes_read = read(fd, c, 1);
        offset += bytes_read;

        wait_sem(queue_per);
        if (c[0] == 'S')
        {
            enqueue(S);
            printf("H has a new homework S; remaining money is %dTL\n", curr_money);
        }
        else if (c[0] == 'Q')
        {
            enqueue(Q);
            printf("H has a new homework Q; remaining money is %dTL\n", curr_money);
        }

        else if (c[0] == 'C')
        {
            enqueue(C);
            printf("H has a new homework C; remaining money is %dTL\n", curr_money);
        }

        if (offset == file_length)
        {
            is_end = 1;
        }

        post_sem(queue_per);
        post_sem(full);

        i1++;
    } while (offset < file_length);

    close(fd);
    if (is_money_end)
        printf("H has no more money for homeworks, terminating.\n");
    else
        printf("H has no other homeworks, terminating.\n");
    pthread_exit(NULL);
}

/*
*   This is hired student thread function
*/
void *st_for_hire(void *arg)
{
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ENABLE, NULL);
    char name[NAME_LEN];
    memset(name, '\0', NAME_LEN);
    strcpy(name, ((students *)arg)->name);
    int c = ((students *)arg)->cost;
    int s = ((students *)arg)->speed;
    //  int q = ((students *)arg)->quality;
    int index = ((students *)arg)->index;
    post_sem(start);
    int hw_num = 0;
    attribute att;
    char mess;
    while (1)
    {

        printf("%s is waiting for a homework\n", name);
        if (read(stu_pipes[index][R], &att, sizeof(att)) == -1)
        {
            fprintf(stderr, "Error read pipe %d : %s\n", index, strerror(errno));
            error_exit();
        }
        if (att == Empty)
            break;
        switch (att)
        {
        case C:
            mess = 'C';
            break;
        case S:
            mess = 'S';
            break;
        default:
            mess = 'Q';
            break;
        }
        hw_num++;
        wait_sem(resource);
        printf("%s is solving homework %c for %d, H has %d TL left \n", name, mess, c, money);
        post_sem(resource);
        sleep(SL_TIME - s);
        wait_sem(resource);
        busy_st[index] = 0;
        post_sem(resource);
        post_sem(empty);
    }

    pthread_exit(NULL);
}

/*
*   This function reads and saves the hired students information
*/
void read_students(char *file_path, char names[][NAME_LEN], int attributes[][ATT_NUM], int *min_cost)
{

    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
    int fd = open(file_path, O_RDONLY, mode);
    if (fd == -1)
    {
        fprintf(stderr, "Error open file : %s\n", strerror(errno));
        error_exit();
    }

    size_t bytes_read, offset = 0;
    char c[1];
    memset(c, '\0', 1);
    off_t file_length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    int i = 0;                    // student index
    int i1 = 0;                   // name index
    int i2 = 0;                   // attribute index
    int fl = 0, fl2 = 1, fl3 = 1; // attribute type(name,speed,cost,quality)
    char num_buffer[NUM_LEN];
    memset(num_buffer, '\0', NUM_LEN);

    do
    {
        bytes_read = read(fd, c, 1);
        offset += bytes_read;

        if ((c[0] == ' ' || c[0] == '\n') && fl2)
            continue;
        else
        {
            switch (fl)
            {
            case 0: // read name
                if (c[0] == ' ')
                {
                    fl++;
                    i1 = 0;
                    i2 = 0;
                    fl2 = 1;
                }
                else
                {
                    fl2 = 0;
                    names[i][i1] = c[0];

                    i1++;
                }

                break;
            case 1: //read quality

                if (c[0] == ' ')
                {
                    attributes[i][i2] = atoi(num_buffer);
                    if (attributes[i][i2] > MAX_S_Q || attributes[i][i2] < MIN_S_Q)
                    {
                        printf("Invalid input file content(Quality must be <6 and >1).\nExiting...\n");
                        error_exit();
                    }
                    memset(num_buffer, '\0', NUM_LEN);
                    fl++;
                    i2++;
                    i1 = 0;
                    fl2 = 1;
                }
                else
                {
                    fl2 = 0;
                    num_buffer[i1] = c[0];
                    i1++;
                }

                break;
            case 2: //read speed
                if (c[0] == ' ')
                {

                    attributes[i][i2] = atoi(num_buffer);
                    if (attributes[i][i2] > MAX_S_Q || attributes[i][i2] < MIN_S_Q)
                    {
                        printf("Invalid input file content(Speed must be <6 and >1).\nExiting...\n");
                        error_exit();
                    }
                    memset(num_buffer, '\0', NUM_LEN);
                    fl++;
                    i2++;
                    i1 = 0;
                    fl2 = 1;
                }
                else
                {
                    fl2 = 0;
                    num_buffer[i1] = c[0];
                    i1++;
                }
                break;
            case 3: //read cost
                if (c[0] == ' ' || c[0] == '\n')
                {

                    attributes[i][i2] = atoi(num_buffer);
                    if (attributes[i][i2] > MAX_C || attributes[i][i2] < MIN_C)
                    {
                        printf("Invalid input file content(Cost must be <1000 and >100).\nExiting...\n");
                        error_exit();
                    }
                    if (fl3)
                    {
                        *min_cost = attributes[i][i2];
                        fl3 = 0;
                    }
                    else if (*min_cost >= attributes[i][i2])
                        *min_cost = attributes[i][i2];
                    memset(num_buffer, '\0', NUM_LEN);
                    fl = 0;
                    i2 = 0;
                    i1 = 0;
                    fl2 = 1;
                    i++;
                }
                else
                {
                    fl2 = 0;
                    num_buffer[i1] = c[0];
                    i1++;
                }
                break;

            default:
                break;
            }
        }

    } while (offset < file_length);

    close(fd);
}

/*
*   This function terminates the program safely when any interrupts occurs
*/
void error_exit()
{

    if (ch_id != -1)
    {
        pthread_cancel(ch_id);

        pthread_join(ch_id, NULL);
    }
    if (st_id != NULL)
    {
        for (size_t i = 0; i < st_num; i++)
        {

            if (st_id[i] != -1)
            {
                pthread_cancel(st_id[i]);
            }
        }
        for (size_t i = 0; i < st_num; i++)
        {
            if (st_id[i] != -1)
            {
                pthread_join(st_id[i], NULL);
            }
        }
    }
    free_resource();
    exit(EXIT_FAILURE);
}

/*
*   This function finds the hired students number in the student file
*/
int find_st_num(char *file_path)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
    int fd = open(file_path, O_RDONLY, mode);
    if (fd == -1)
    {
        fprintf(stderr, "Error open file : %s\n", strerror(errno));
        error_exit();
    }

    size_t bytes_read, offset = 0;
    char c[1];
    memset(c, '\0', 1);
    off_t file_length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    int num = 0;
    int fl = 0;
    do
    {
        bytes_read = read(fd, c, 1);
        if (c[0] != ' ' && c[0] != '\n')
            fl = 1;
        if (c[0] == '\n')
        {
            if (fl)
                num++;
            fl = 0;
        }

        offset += bytes_read;

    } while (offset < file_length);

    close(fd);

    return num;
}

/*
*   This is SIGINT signal hanler
*/
void handler(int signal)
{

    int savedErrno = errno;
    error_exit();
    errno = savedErrno;
}
