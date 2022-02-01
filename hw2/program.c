#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/file.h>
#include <math.h>

#define LINE_LENGTH 1024 // Max line length
#define NUM_LENGTH 15    // Max number length
#define CHILDNUM 8       // Child procces number
#define ROW_NUM 8        // Calculated row number

sig_atomic_t living_child = CHILDNUM;     // Number of living children
sig_atomic_t first_calc_child = CHILDNUM; // The number of children who could not finish the first calculation
pid_t cpids[CHILDNUM];                    // Child processes id

void freePtr(char *ptr);
void read_file2();
void handler(int sig);
void end_child();
void do_fork(int fd);

void convert_num(char *buffer, double *numbers);
void print(double num[], int size);
void child_read(int fd, int chld_num, int calc_num);
void lock_file(int fd);
void unlock_file(int fd);
double calc(double *nums, int num_len);
void append_line_end(char *file_cont, char *new_ele, off_t point, int fd);
double calc_func(double coefficients[], int size, double x_val);
void seperate_x_y(double *numbers, int size, int calc_num, double x[], double y[]);
double parent_read(int fd, int calc_num);
double find_average(double *numbers, int size);
double find_error(double *num, int calc_num);
double *r8vec_copy_new(int n, double a1[]);
double *dvand(int n, double alpha[], double b[]);
int main(int argc, char *argv[])
{

    if (argv[1] == NULL || strcmp(argv[1], " ") == 0)
    {
        printf("Invalid input type !!!\n");
        exit(EXIT_FAILURE);
    }

    memset(cpids, 0, CHILDNUM * sizeof(pid_t));

    sigset_t block_mask, empty_mask;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("Failed to install SIGCHLD signal handler");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("Failed to install SIGINT signal handler");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGUSR1, &sa, NULL) == -1)
    {
        perror("Failed to install SIGUSR1 signal handler");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGUSR2, &sa, NULL) == -1)
    {
        perror("Failed to install SIGUSR2 signal handler");
        exit(EXIT_FAILURE);
    }

    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGCHLD);
    sigaddset(&block_mask, SIGUSR1);
    if (sigprocmask(SIG_SETMASK, &block_mask, NULL) == -1)
    {
        fprintf(stderr, "sigprocmask : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;

    int fd = open(argv[1], O_RDWR, mode);
    if (fd == -1)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }

    do_fork(fd);
    sigemptyset(&empty_mask);
    while (first_calc_child > 0)
    {

        if (sigsuspend(&empty_mask) == -1 && errno != EINTR)
        {
            end_child();
            fprintf(stderr, "sigsuspend : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    printf("Error of polynomial of degree 5 : %.1f \n", parent_read(fd, 1));

    for (size_t i = 0; i < CHILDNUM; i++)
    {
        kill(cpids[i], SIGUSR2);
    }

    sigemptyset(&empty_mask);
    while (living_child > 0)
    {

        if (sigsuspend(&empty_mask) == -1 && errno != EINTR)
        {
            end_child();
            fprintf(stderr, "sigsuspend : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    printf("Error of polynomial of degree 6 : %.1f \n", parent_read(fd, 2));

    close(fd);


    return 0;
}

/*
*   This function creates child processes
*/
void do_fork(int fd)
{

    cpids[0] = 12;

    for (size_t i = 0; i < CHILDNUM; i++)
    {

        switch (cpids[i] = fork())
        {
        case -1:
            fprintf(stderr, "fork : %s\n", strerror(errno));
            end_child();
            exit(EXIT_FAILURE);
        case 0:

            child_read(fd, i, 1);
            kill(getppid(), SIGUSR1);
            sigset_t sig_set;
            sigemptyset(&sig_set);
            sigdelset(&sig_set, SIGUSR2);

            if (sigsuspend(&sig_set) == -1 && errno != EINTR)
            {
                end_child();
                fprintf(stderr, "sigsuspend : %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            child_read(fd, i, 2);

            exit(EXIT_SUCCESS);
            break;
        default:

            break;
        }
    }
}

/*
*   This function adds the file content after the corresponding line.
*/
void append_line_end(char *file_cont, char *new_ele, off_t point, int fd)
{

    lseek(fd, point, SEEK_SET);
    write(fd, new_ele, strlen(new_ele));
    lseek(fd, point + strlen(new_ele), SEEK_SET);
    if (file_cont != NULL)
        write(fd, file_cont, strlen(file_cont));
}

/*
*   This function reads and make calculation corresponding line according to child process
*/
void child_read(int fd, int chld_num, int calc_num)
{

    lock_file(fd);

    size_t bytes_read, offset = 0;
    char c[1];
    memset(c, '\0', 1);
    int new_line = 0; // line number counter
    int i = 0;        // buffer array index
    int com_num = 0;  //comma number
    off_t break_point = -1;
    off_t file_length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char buffer[LINE_LENGTH];
    memset(buffer, '\0', LINE_LENGTH);
    char *file_content = NULL;
    int i2 = 0;
    do
    {
        bytes_read = read(fd, c, 1);

        if (chld_num < new_line)
        {
            if (break_point == -1)
            {
                break_point = offset - 1;
                file_content = (char *)malloc(sizeof(char) * (file_length - offset) + 2);
                memset(file_content, '\0', file_length - offset + 2);
                file_content[i2] = '\n';
                i2++;
            }

            file_content[i2] = c[0];
            i2++;
        }

        else if (chld_num == new_line)
        {
            buffer[i] = c[0];
            i++;
            if (c[0] == ',')
                com_num++;
        }

        if (c[0] == '\n')
            new_line++;
        offset += bytes_read;

    } while (offset < file_length);
    com_num++;

    double numbers[com_num];
    memset(numbers, 0, com_num);
    convert_num(buffer, numbers);
    double *x = NULL, *y = NULL;
    int coor_size = 0;
    if (calc_num == 1)
    {
        coor_size = ROW_NUM - 2;
        x = (double *)malloc(coor_size * sizeof(double));
        y = (double *)malloc(coor_size * sizeof(double));
    }
    else
    {
        coor_size = ROW_NUM - 1;
        x = (double *)malloc(coor_size * sizeof(double));
        y = (double *)malloc(coor_size * sizeof(double));
    }
    memset(x, 0, coor_size * sizeof(double));
    memset(y, 0, coor_size * sizeof(double));
    seperate_x_y(numbers, com_num, calc_num, x, y);
    double *coeff = dvand(coor_size, x, y);

    if (calc_num == 2)
    {
        printf("Polynomial %d: ", chld_num);
        for (size_t i3 = 0; i3 < coor_size; i3++)
        {
            printf("%.1f, ", coeff[i3]);
        }
        printf("\n");
    }
    double res = calc_func(coeff, coor_size, numbers[14]);

    char ch_res[10];
    memset(ch_res, '\0', 10);
    char ch_res2[10];
    memset(ch_res2, '\0', 10);
    strcpy(ch_res2, ",");
    snprintf(ch_res, 10, "%.1f", res);
    strcat(ch_res2, ch_res);
    append_line_end(file_content, ch_res2, break_point, fd);
    freePtr(file_content);
    free(coeff);
    free(x);
    free(y);
    unlock_file(fd);
}

double *dvand(int n, double alpha[], double b[])

/******************************************************************************/
/*
  Purpose:

    DVAND solves a Vandermonde system A' * x = b.

  Licensing:

    This code is distributed under the GNU LGPL license.

  Modified:

    23 February 2014

  Author:

    John Burkardt

  Reference:

    Ake Bjorck, Victor Pereyra,
    Solution of Vandermonde Systems of Equations,
    Mathematics of Computation,
    Volume 24, Number 112, October 1970, pages 893-903.

  Parameters:

    Input, int N, the order of the matrix.

    Input, double ALPHA[N], the parameters that define the matrix.
    The values should be distinct.

    Input, double B[N], the right hand side of the linear system.

    Output, double DVAND[N], the solution of the linear system.
*/
{
    int j;
    int k;
    double *x;

    x = r8vec_copy_new(n, b);

    for (k = 0; k < n - 1; k++)
    {
        for (j = n - 1; k < j; j--)
        {
            x[j] = (x[j] - x[j - 1]) / (alpha[j] - alpha[j - k - 1]);
        }
    }

    for (k = n - 2; 0 <= k; k--)
    {
        for (j = k; j < n - 1; j++)
        {
            x[j] = x[j] - alpha[k] * x[j + 1];
        }
    }

    return x;
}

double *r8vec_copy_new(int n, double a1[])

/******************************************************************************/
/*
  Purpose:

    R8VEC_COPY_NEW copies an R8VEC.

  Discussion:

    An R8VEC is a vector of R8's.

  Licensing:

    This code is distributed under the GNU LGPL license.

  Modified:

    26 August 2008

  Author:

    John Burkardt

  Parameters:

    Input, int N, the number of entries in the vectors.

    Input, double A1[N], the vector to be copied.

    Output, double R8VEC_COPY_NEW[N], the copy of A1.
*/
{
    double *a2;
    int i;

    a2 = (double *)malloc(n * sizeof(double));

    for (i = 0; i < n; i++)
    {
        a2[i] = a1[i];
    }
    return a2;
}

/*
 *   This function seperate x and y numbers
 */
void seperate_x_y(double *numbers, int size, int calc_num, double x[], double y[])
{

    int in = 0;
    for (size_t i = 0; i < size; i++)
    {
        if (calc_num == 1 && in == ROW_NUM - 2)
            break;
        else if (calc_num == 2 && in == ROW_NUM - 1)
            break;
        if (i % 2 == 0)
            x[in] = numbers[i];
        else
        {

            y[in] = numbers[i];

            in++;
        }
    }
}

/*
*   This function calculate polynomial value
*/
double calc_func(double coefficients[], int size, double x_val)
{

    double res = 0, temp = 0;
    for (size_t i = 0; i < size; i++)
    {
        temp = 0;
        temp = pow(x_val, i);
        temp *= coefficients[i];
        res += temp;
    }
    return res;
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
*   This function convert char to double
*/
void convert_num(char *buffer, double *numbers)
{
    char *num;
    char *ptr;
    char *token = ",";
    double res;
    num = strtok(buffer, token);
    int i = 0;

    while (num != NULL)
    {
        res = strtod(num, &ptr);
        numbers[i] = res;
        num = strtok(NULL, ",");
        i++;
    }
}

/*
*   This is SIGCHLD, SIGINT, SIGUSR1 signal hanler
*/
void handler(int signal)
{

    int savedErrno = errno;

    if (signal == SIGINT)
    {
        int i;
        for (i = 0; i < CHILDNUM; i++)
        {
            kill(cpids[i], SIGKILL);
            printf("Child Process  terminated (pid = %d)\n", cpids[i]);
        }

        exit(EXIT_SUCCESS);
    }

    else if (signal == SIGUSR1 && first_calc_child > 0)
    {
        first_calc_child--;
    }

    /*
    *   This part taken from page 557 of the textbook
    */
    else if (signal == SIGCHLD)
    {
        int status;
        pid_t cpid;

        while ((cpid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            living_child--;
        }

        if (cpid == -1 && errno != ECHILD)
        {
            fprintf(stderr, "waitpid \n");
            exit(EXIT_FAILURE);
        }
    }

    errno = savedErrno;
}

/*
*   This function kill the child process
*/
void end_child()
{

    for (int i = 0; i < CHILDNUM; i++)
    {
        kill(cpids[i], SIGKILL);
        printf("Child Process  terminated (pid = %d)\n", cpids[i]);
    }
}

/*
*   This function lock the file
*/
void lock_file(int fd)
{

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_pid = getpid();

    if (fcntl(fd, F_SETLKW, &fl) == -1)
    {
        end_child();
        printf("%s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/*
*   This function unlock the file
*/
void unlock_file(int fd)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_pid = getpid();

    if (fcntl(fd, F_SETLKW, &fl) == -1)
    {
        end_child();
        printf("%s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

/*
*   This function read file and make calculation according to parent process
*/
double parent_read(int fd, int calc_num)
{
    lock_file(fd);

    size_t bytes_read, offset = 0;
    char c[1];
    memset(c, '\0', 1);
    int i = 0;       // buffer array index
    int com_num = 0; //comma number
    off_t file_length = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char buffer[LINE_LENGTH];
    memset(buffer, '\0', LINE_LENGTH);
    double errors[ROW_NUM];
    memset(errors, 0, ROW_NUM * sizeof(double));
    int i2 = 0;
    do
    {

        bytes_read = read(fd, c, 1);

        buffer[i] = c[0];
        i++;
        if (c[0] == ',')
            com_num++;
        if (c[0] == '\n' || offset == file_length - 1)
        {
            com_num++;
            double numbers[com_num];
            memset(numbers, 0, com_num);
            convert_num(buffer, numbers);
            errors[i2] = find_error(numbers, calc_num);
            memset(buffer, '\0', LINE_LENGTH);
            i = 0;
            com_num = 0;
            i2++;
        }
        offset += bytes_read;

    } while (offset < file_length);
    unlock_file(fd);

    return find_average(errors, ROW_NUM);
}

/*
*   This function calculates the average of array numbers
*/
double find_average(double *numbers, int size)
{
    double res = 0;
    for (size_t i = 0; i < size; i++)
    {
        res += numbers[i];
    }
    return res / size;
}

/*
*   This function find the absolute estimation error
*/
double find_error(double *num, int calc_num)
{
    double res = 0;
    if (calc_num == 1)
        res = num[15] - num[16];

    else
        res = num[15] - num[17];

    if (res < 0)
        res *= -1;

    return res;
}
