
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/file.h>

#define BUFF_SIZE 256   /* fifo size */
#define SHM_SIZE 1024   /* shared memory size */
#define SHM_LINE_LEN 32 /* shared memory line len*/
#define LINE_LENGTH 128 /* max line lenght in fifo name file */
#define FILE_SIZE 1024  /* max file size */
#define NUM_LENGTH 10   /* max patato number */

void lock_file(int fd);
void unlock_file(int fd);
void freePtr(char *ptr);
void first_write(int fd, char *fifo_path);
void first_read(int fd, char *fifo_path);
char *read_fifo_name(char *file_name, char *f_name, int *f_count, int *is_last);
char *open_shm(char *sm_name, int flag);
void close_shm(char *sm_name, char *addr, int flag);
int open_sem(char *sem_name, int in_val, sem_t *sem);
void close_sem(sem_t *sem, char *sem_name, int flag);
void choose_fifo(char *all_names, int f_count, char *choosen_name, char *f_name, int choice, int line);
void str2int(char *buffer, int *nums);
void int2str(int *numbers, char *buffer);
int read_fifo(char *f_name, char *buff);
int write_fifo(char *f_name, char *mess);
int first_write_sm(char *addr, char *mess, int is_last);
int write_sm(char *addr, int pid, int *rem);
int check_other(char *addr);
int check_alive(char *addr);
int main(int argc, char *argv[])
{

    char *addr = NULL;
    int flag = 1, f_count = 0, is_last = 0;
    int rem;
    int opt;

    char *sm_name = NULL, *sem_name = NULL, *file_path = NULL;
    int patato_num = -1;
    while ((opt = getopt(argc, argv, "b:s:f:m:")) != -1)
    {
        switch (opt)
        {

        case 'b':

            if (optarg != NULL)
            {
                patato_num = atoi(optarg);
            }

            break;

        case 's':

            if (optarg != NULL)
            {

                sm_name = (char *)malloc(strlen(optarg) + 1);
                strcpy(sm_name, optarg);
            }
            break;
        case 'f':

            if (optarg != NULL)
            {

                file_path = (char *)malloc(strlen(optarg) + 1);
                strcpy(file_path, optarg);
            }
            break;
        case 'm':

            if (optarg != NULL)
            {

                sem_name = (char *)malloc(strlen(optarg) + 1);
                strcpy(sem_name, optarg);
            }
            break;

        case '?':
            fprintf(stderr, "Exiting...\n");
            freePtr(sm_name);
            freePtr(sem_name);
            freePtr(file_path);
            exit(EXIT_FAILURE);
            break;
        }
    }

    sem_t *sem = sem_open(sem_name, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1);
    if (sem == SEM_FAILED)
    {

        if (errno == EEXIST)
        {
            sem = sem_open(sem_name, 0);
            flag = 0;
        }
        else
        {
            fprintf(stderr, "Error opening semaphore: %s\n", strerror(errno));
            freePtr(sm_name);
            freePtr(sem_name);
            freePtr(file_path);
            exit(EXIT_FAILURE);
        }
    }
    addr = open_shm(sm_name, flag);
    if (addr == NULL)
    {
        close_sem(sem, sem_name, flag);
        close_shm(sm_name, addr, flag);
        freePtr(sm_name);
        freePtr(sem_name);
        freePtr(file_path);
        return -1;
    }
    int pid = getpid();

    if (sem_wait(sem) == -1)
    {
        fprintf(stderr, "Error waiting semaphore: %s\n", strerror(errno));
        freePtr(sm_name);
        freePtr(sem_name);
        freePtr(file_path);
        exit(EXIT_FAILURE);
    }

    char f_name[LINE_LENGTH]; //process fifo name
    memset(f_name, '\0', LINE_LENGTH);

    char *all_names = read_fifo_name(file_path, f_name, &f_count, &is_last);

    if (all_names == NULL)
    {
        printf("No more fifo path\n");
        close_sem(sem, sem_name, flag);
        close_shm(sm_name, addr, flag);
        freePtr(sm_name);
        freePtr(sem_name);
        freePtr(file_path);
        exit(EXIT_FAILURE);
    }

    char sm_buff[SHM_LINE_LEN];
    memset(sm_buff, '\0', SHM_LINE_LEN);
    int numbers[2];
    numbers[0] = pid;
    numbers[1] = patato_num;
    int2str(numbers, sm_buff);

    int my_line = first_write_sm(addr, sm_buff, is_last);
    int next = 1;
    if (sem_post(sem) == -1)
    {
        fprintf(stderr, "Error posting semaphore: %s\n", strerror(errno));
        freePtr(all_names);
        freePtr(sm_name);
        freePtr(sem_name);
        freePtr(file_path);
        close_sem(sem, sem_name, is_last);
        close_shm(sm_name, addr, is_last);
        exit(EXIT_FAILURE);
    }
    printf("Proccess : %d - %s  started with %d \n", pid, f_name, patato_num);

    if (is_last)
    {
        char chosen_fifo[LINE_LENGTH];

        for (size_t i = 0; i < f_count - 1; i++)
        {
            memset(chosen_fifo, '\0', LINE_LENGTH);
            choose_fifo(all_names, f_count, chosen_fifo, f_name, i, 0);

            char *mess = "start";
            if (write_fifo(chosen_fifo, mess) == -1)
            {
                freePtr(all_names);
                freePtr(sm_name);
                freePtr(sem_name);
                freePtr(file_path);
                close_sem(sem, sem_name, is_last);
                close_shm(sm_name, addr, is_last);
                exit(EXIT_FAILURE);
            }
        }
    }
    else
    {
        char f_buff[BUFF_SIZE];
        memset(f_buff, '\0', BUFF_SIZE);
        if (read_fifo(f_name, f_buff) == -1)
        {
            freePtr(all_names);
            freePtr(sm_name);
            freePtr(sem_name);
            freePtr(file_path);
            close_sem(sem, sem_name, is_last);
            close_shm(sm_name, addr, is_last);
            exit(EXIT_FAILURE);
        }
    }

    if (sem_wait(sem) == -1)
    {
        fprintf(stderr, "Error waiting semaphore: %s\n", strerror(errno));
        freePtr(all_names);
        freePtr(sm_name);
        freePtr(sem_name);
        freePtr(file_path);
        close_sem(sem, sem_name, is_last);
        close_shm(sm_name, addr, is_last);
        exit(EXIT_FAILURE);
    }

    char chosen_fifo[LINE_LENGTH];
    memset(chosen_fifo, '\0', LINE_LENGTH);
    char prev_fifo[LINE_LENGTH];

    next += my_line;
    next = next % f_count;
    if (next == my_line)
    {
        next++;
        next = next % f_count;
    }

    choose_fifo(all_names, f_count, chosen_fifo, f_name, next, my_line);
    memcpy(prev_fifo, chosen_fifo, LINE_LENGTH);
    char f_buff[BUFF_SIZE];
    memset(f_buff, '\0', BUFF_SIZE);
    int2str(numbers, f_buff);
    int f_count2 = f_count;
    if (patato_num != 0)
    {
        if (write_fifo(chosen_fifo, f_buff) == -1)
        {
            freePtr(all_names);
            freePtr(sm_name);
            freePtr(sem_name);
            freePtr(file_path);
            close_sem(sem, sem_name, is_last);
            close_shm(sm_name, addr, is_last);
            return -1;
        }
    }

    if (sem_post(sem) == -1)
    {
        freePtr(all_names);
        freePtr(sm_name);
        freePtr(sem_name);
        freePtr(file_path);
        close_sem(sem, sem_name, is_last);
        close_shm(sm_name, addr, is_last);
        fprintf(stderr, "Error posting semaphore: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("pid = %d sending potato number %d to %s remaind %d\n", pid, pid, chosen_fifo, patato_num);

    int fl2 = 1;
    while (1)
    {

        memset(f_buff, '\0', BUFF_SIZE);

        if (read_fifo(f_name, f_buff) == -1)
        {
            freePtr(all_names);
            freePtr(sm_name);
            freePtr(sem_name);
            freePtr(file_path);
            close_sem(sem, sem_name, is_last);
            close_shm(sm_name, addr, is_last);
            exit(EXIT_FAILURE);
        }

        if (sem_wait(sem) == -1)
        {
            freePtr(all_names);
            freePtr(sm_name);
            freePtr(sem_name);
            freePtr(file_path);
            close_sem(sem, sem_name, is_last);
            close_shm(sm_name, addr, is_last);
            fprintf(stderr, "Error waiting semaphore: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        str2int(f_buff, numbers);
        if (numbers[0] == 0)
        {

            if (check_alive(addr) == 1)
                is_last = 1;
            else
                is_last = 0;

            if (sem_post(sem) == -1)
            {
                freePtr(all_names);
                freePtr(sm_name);
                freePtr(sem_name);
                freePtr(file_path);
                close_sem(sem, sem_name, is_last);
                close_shm(sm_name, addr, is_last);
                fprintf(stderr, "Error posting semaphore: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
            break;
        }

        printf("pid = %d receiving potato number %d from %s\n", pid, numbers[0], f_name);

        if (write_sm(addr, numbers[0], &rem) == 0)
        {

            printf("pid = %d; potato number %d has cooled down.\n", pid, numbers[0]);

            for (size_t i = 0; i < f_count; i++)
            {
                memset(chosen_fifo, '\0', LINE_LENGTH);
                choose_fifo(all_names, f_count, chosen_fifo, f_name, i, my_line);
                printf("choosen fifo : -%s- \n", chosen_fifo);
                if (strcmp(f_name, chosen_fifo) == 0)
                    continue;

                char *mess = "start";
                if (write_fifo(chosen_fifo, mess) == -1)
                {
                    freePtr(all_names);
                    freePtr(sm_name);
                    freePtr(sem_name);
                    freePtr(file_path);
                    close_sem(sem, sem_name, is_last);
                    close_shm(sm_name, addr, is_last);
                    exit(EXIT_FAILURE);
                }
            }

            is_last = 0;
            fl2 = 0;
        }
        if (rem <= 0)
            rem = 0;

        if (sem_post(sem) == -1)
        {
            fprintf(stderr, "Error posting semaphore: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (fl2)
        {
            if (rem)
            {
                if (sem_wait(sem) == -1)
                {
                    freePtr(all_names);
                    freePtr(sm_name);
                    freePtr(sem_name);
                    freePtr(file_path);
                    close_sem(sem, sem_name, is_last);
                    close_shm(sm_name, addr, is_last);
                    fprintf(stderr, "Error waiting semaphore: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                memset(chosen_fifo, '\0', LINE_LENGTH);

                while (1)
                {
                    next += my_line;
                    next = next % f_count;
                    if (next == my_line)
                    {
                        next++;
                        next = next % f_count;
                    }
                    choose_fifo(all_names, f_count, chosen_fifo, f_name, -1, my_line);
                    if (strcmp(chosen_fifo, prev_fifo) != 0 || f_count2 <= 1)
                        break;
                }

                memcpy(prev_fifo, chosen_fifo, LINE_LENGTH);
                numbers[1] = rem;
                memset(f_buff, '\0', BUFF_SIZE);
                int2str(numbers, f_buff);
                printf("pid = %d sending potato number %d to %s remaind %d\n", pid, numbers[0], chosen_fifo, numbers[1]);

                if (write_fifo(chosen_fifo, f_buff) == -1)
                {
                    freePtr(all_names);
                    freePtr(sm_name);
                    freePtr(sem_name);
                    freePtr(file_path);
                    close_sem(sem, sem_name, is_last);
                    close_shm(sm_name, addr, is_last);
                    exit(EXIT_FAILURE);
                }
                if (sem_post(sem) == -1)
                {
                    freePtr(all_names);
                    freePtr(sm_name);
                    freePtr(sem_name);
                    freePtr(file_path);
                    close_sem(sem, sem_name, is_last);
                    close_shm(sm_name, addr, is_last);
                    fprintf(stderr, "Error posting semaphore: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                printf("pid = %d; potato number %d has cooled down.\n", pid, numbers[0]);
                f_count2--;
            }
        }
        else
            break;
    }

    freePtr(all_names);
    close_sem(sem, sem_name, is_last);
    close_shm(sm_name, addr, is_last);

    exit(EXIT_SUCCESS);
}

/*
*   This function checks other processes are running or not
*/
int check_alive(char *addr)
{

    char *num;
    char *token = "-";
    int res;
    char buffer[SHM_SIZE];
    memcpy(buffer, addr, SHM_SIZE);
    num = strtok(buffer, token);

    while (num != NULL)
    {

        res = atoi(num);
        num = strtok(NULL, token);
    }
    res--;

    int i2 = 0;
    for (size_t i = 0; addr[i] != '\0'; i++)
    {
        if (addr[i] == '\n')
            i2 = i + 1;
    }
    char num1[NUM_LENGTH];
    memset(num1, '\0', NUM_LENGTH);
    snprintf(num1, NUM_LENGTH, "%d", res);
    for (size_t i = 0; num1[i] != '\0'; i++)
    {
        addr[i2] = num1[i];
        i2++;
    }
    addr[i2] = '\0';
    if (res == 0)
        return 0;
    return res;
}

/*
*   This function converts pid and switch number to string for writing fifo and shared memory
*/
void int2str(int *numbers, char *buffer)
{
    char num1[NUM_LENGTH];
    char num2[NUM_LENGTH];
    memset(num1, '\0', NUM_LENGTH);
    memset(num2, '\0', NUM_LENGTH);
    snprintf(num1, NUM_LENGTH, "%d", numbers[0]);
    snprintf(num2, NUM_LENGTH, "%d", numbers[1]);
    strcpy(buffer, num1);
    strcat(buffer, "-");
    strcat(buffer, num2);
    strcat(buffer, "-");
    strcat(buffer, "\n");
}

/*
*   This function converts pid and switch number to number decrement switch num
*/
void str2int(char *buffer, int *numbers)
{

    char *num;
    char *token = "-";
    int res;
    num = strtok(buffer, token);
    int i = 0;

    while (num != NULL)
    {
        res = atoi(num);
        numbers[i] = res;
        num = strtok(NULL, token);
        i++;
    }
}

/*
*   This function reads fifo
*/
int read_fifo(char *f_name, char *buff)
{
    int fd = open(f_name, O_RDONLY);

    if (fd == -1)
    {
        fprintf(stderr, "Error opening fifo: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    if (read(fd, buff, BUFF_SIZE) == -1)
    {
        fprintf(stderr, "Error readingaa fifo: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    //  close(fd);
    return 0;
}

/*
*   This function writes message to fifo
*/
int write_fifo(char *f_name, char *mess)
{

    int fd = open(f_name, O_WRONLY);

    if (fd == -1)
    {
        fprintf(stderr, "Error opening fifo: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    int mess_size = write(fd, mess, strlen(mess) + 1);

    if (mess_size != strlen(mess) + 1)
    {
        fprintf(stderr, "Error writing fifo: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    //close(fd);

    return 0;
}

/*
*   This function chooses a fifo randomly, if choice != -1 then chooses choice index 
*/
void choose_fifo(char *all_names, int f_count, char *choosen_name, char *f_name, int choice, int line)
{

    srand(time(NULL));
    while (1)
    {
        int r_num;
        if (choice == -1)
        {
            r_num = (rand() + line);
            r_num = r_num % f_count;
        }

        else
            r_num = choice;

        int in = 0, size = strlen(all_names), i2 = 0;
        for (size_t i = 0; i < size; i++)
        {
            if (all_names[i] == '\n' || i == size - 1)
            {

                in++;
                i2 = 0;
                if (in > r_num)
                    break;
                continue;
            }
            if (in == r_num)
            {

                choosen_name[i2] = all_names[i];
                i2++;
            }
        }

        if (strcmp(f_name, choosen_name) != 0 || choice != -1)
            break;

        memset(choosen_name, '\0', LINE_LENGTH);
    }
}

/*
*   This function closes semaphore 
*/
void close_sem(sem_t *sem, char *sem_name, int flag)
{
    if (sem_close(sem) == -1)
    {
        fprintf(stderr, "Error closing semaphore: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (flag)
    {
        if (sem_unlink(sem_name) == -1)
        {
            fprintf(stderr, "Error unlinking semaphore: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
}

/*
*   This function opens semaphore 
*/
char *open_shm(char *sm_name, int flag)
{
    char *addr;
    int smd = shm_open(sm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (smd == -1)
    {
        if (errno != EEXIST)
        {

            fprintf(stderr, "Error opening shared memory: %s\n", strerror(errno));
            return NULL;
        }
        else
        {
            smd = shm_open(sm_name, O_RDWR, S_IRUSR | S_IWUSR);
        }
    }
    if (flag)
    {
        if (ftruncate(smd, SHM_SIZE) == -1)
        {
            fprintf(stderr, "Error resize shared memory: %s\n", strerror(errno));
            return NULL;
        }
    }

    addr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, smd, 0);
    if (addr == MAP_FAILED)
    {
        fprintf(stderr, "Error map shared memory: %s\n", strerror(errno));
        return NULL;
    }
    if (flag)
        memset(addr, '\0', SHM_SIZE);

    close(smd);
    return addr;
}

/* 
*   This function closes shared memory 
*/
void close_shm(char *sm_name, char *addr, int flag)
{
    if (munmap(addr, SHM_SIZE) == -1)
    {
        fprintf(stderr, "Error munmap shared memory: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (flag)
    {

        if (shm_unlink(sm_name) == -1)
        {
            fprintf(stderr, "Error unlinking shared memory: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
}

/*
*   This function reads fifo names from file
*/
char *read_fifo_name(char *file_name, char *f_name, int *f_count, int *is_last)
{
    mode_t mode = S_IRUSR | S_IRGRP | S_IROTH;

    int fd = open(file_name, O_RDONLY, mode);
    if (fd == -1)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }

    size_t bytes_read, offset = 0;
    char c[1];
    memset(c, '\0', 1);

    int i = 0; // buffer array index

    off_t file_length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char *all_names = (char *)malloc(sizeof(char) * file_length + 1);
    memset(all_names, '\0', file_length + 1);
    int i2 = 0; // all_names index
    int flag = 0;
    *f_count = 0;
    do
    {
        bytes_read = read(fd, c, 1);
        all_names[i2] = c[0];
        i2++;
        if (c[0] == '\n' || offset == file_length - 1)
        {
            *f_count += 1;
        }

        if ((c[0] == '\n' || offset == file_length - 1) && flag == 0)
        {

            if (mkfifo(f_name, S_IRUSR | S_IWUSR) == -1)
            {
                if (errno != EEXIST)
                {
                    fprintf(stderr, "Error opening fifo: %s\n", strerror(errno));
                    freePtr(all_names);
                    return NULL;
                }

                memset(f_name, '\0', LINE_LENGTH);
                i = 0;
            }
            else
            {
                if (offset == file_length - 1)
                    *is_last = 1;
                flag = 1;
            }
        }
        if (flag == 0 && c[0] != '\n')
        {
            f_name[i] = c[0];
            i++;
        }

        offset += bytes_read;

    } while (offset < file_length);
    if (f_name[0] == '\0')
    {
        freePtr(all_names);
        return NULL;
    }
    return all_names;
}

/*
*   This function writes potato number to shared memory at the beginning of the program
*/
int first_write_sm(char *addr, char *mess, int is_last)
{

    int p_line_num = 0;
    if (addr[0] == '\0') //ilk yazis
    {
        strcpy(addr, mess);
        p_line_num = 0;
    }
    else
    {
        for (size_t i = 0; addr[i] != '\0'; i++)
        {
            if (addr[i] == '\n')
                p_line_num++;
        }

        strcat(addr, mess);
    }
    if (is_last) // write patato number end of the shared memory
    {

        char num1[NUM_LENGTH];
        memset(num1, '\0', NUM_LENGTH);
        snprintf(num1, NUM_LENGTH, "%d", (p_line_num + 1));
        strcat(addr, num1);
    }
    return p_line_num;
}

/*
*   This function checks other  potato number is zero or not  
*/
int check_other(char *addr)
{
    char *num;
    char *token = "-";
    int res;
    char buffer[SHM_SIZE];
    memcpy(buffer, addr, SHM_SIZE);
    num = strtok(buffer, token);
    int i = 0;
    while (num != NULL)
    {
        i++;
        res = atoi(num);
        if (i % 2 == 0 && res != 0)
        {
            return -1;
        }

        num = strtok(NULL, token);
    }
    return 0;
}

/*
*   This function writes potato number and swith to shared memory
*/
int write_sm(char *addr, int pid, int *rem)
{
    char *num;
    char *token = "-";
    int res;
    char buffer[SHM_SIZE];
    memcpy(buffer, addr, SHM_SIZE);
    char buffer3[SHM_SIZE];
    memset(buffer3, '\0', SHM_SIZE);
    num = strtok(buffer, token);
    int fl = 0;
    while (num != NULL)
    {
        res = atoi(num);
        if (fl)
            break;
        if (res == pid)
        {
            fl = 1;
        }
        num = strtok(NULL, token);
    }

    res--;
    *rem = res;
    char num1[NUM_LENGTH];
    memset(num1, '\0', NUM_LENGTH);
    snprintf(num1, NUM_LENGTH, "%d", res);

    int point = 0;

    char buffer2[SHM_LINE_LEN];
    memset(buffer2, '\0', SHM_LINE_LEN);
    for (size_t i = 0; addr[i] != '\0'; i++)
    {
        int in = 0;

        memset(buffer2, '\0', SHM_LINE_LEN);
        size_t i2 = i;
        for (; addr[i2] != '\0'; i2++)
        {
            if (addr[i2] == '\n')
                continue;
            if (addr[i2] == '-')
            {
                break;
            }

            buffer2[in] = addr[i2];
            in++;
        }

        int t = atoi(buffer2);

        if (t == pid)
        {

            point = i2 + 1;
            int i3 = i2 + 1;
            while (addr[i3] != '-' && addr[i3] != '\0')
            {
                i3++;
            }

            int in2 = 0;
            for (size_t i4 = i3; addr[i4] != '\0'; i4++)
            {
                buffer3[in2] = addr[i4];
                in2++;
            }

            break;
        }
        i = i2;
    }

    for (size_t i = 0; num1[i] != '\0'; i++)
    {
        addr[point] = num1[i];
        point++;
    }
    for (size_t i = 0; buffer2[i] != '\0'; i++)
    {
        addr[point] = buffer3[i];
        point++;
    }

    return check_other(addr);
}

/*
*   This function free the char pointers
*/
void freePtr(char *ptr)
{
    if (ptr != NULL)
        free(ptr);
}
