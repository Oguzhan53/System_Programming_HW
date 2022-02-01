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
#include <signal.h>

#include <sys/wait.h>

#define CLINICSTORE "clinic_store"         // Clinic shared memory(nurse - vaccinator) address
#define CLINICBUFFARR "clinic_bufer_array" // Clinic buffer share memory address
#define CITIZENVACNUM "citizen_vac_num"    // Citizens remaininig shots number address
#define VACROOM "vaccine_room"             // Vaccine room shared memor address
#define DOSENUMBER "doing_dose_number"     // Number of vaccine doses made by the vaccinator shared memory address

#define PIPEBUFSIZE 64    // Max pipe buffer size
#define FILEPATHSIZE 1024 // Max file path size
#define R 0               // Reading end of pipes
#define W 1               // Writing end of pipes

typedef struct
{
    sem_t mutex, empty, full; // Semaphores
    int size;                 // Number of vaccine in the clinic
    int f;                    // Number of '1' vaccine
    int s;                    // Number of '2' vaccine
    int is_end;               // Vaccine end flag
    int rem_nur_num;          // Living nurse processes
} CBuffer;

typedef struct
{
    sem_t mutex, full, start; // Semaphores
    int rem_c_num;            // Living citizen processes
} VacRoom;

sig_atomic_t living_child; // Living child number
CBuffer *clinic;           // Clinic shared memory struct
VacRoom *vac_room;         // Vaccine room shared memory struct
int v_num = -1;            // Numver of vaccinators
int n_num = -1;            // Numver of nurses
int b_size = -1;           // Size of buffer
int c_num = -1;            // Numver of citizens
int t_num = -1;            // Number of vaccine shots

char *cl_buff_arr = NULL; // Clinic buffer shared memory
int *cit_vac_num = NULL;  // Citizens remaining vacinie number
int *vac_dose_num = NULL; // Vacciantors vaccinated dose number
char file_path[FILEPATHSIZE];
void create_nurse(int fd, pid_t n_pids[], int size, int cit_pipes[][2], int vac_pipes[][2]);
void create_vaccinator(pid_t v_pids[], int size, int cit_pipes[][2], int vac_pipes[][2]);
void create_citizen(int size, int cit_pipes[][2], int vac_pipes[][2]);
void create_clinic();
void create_vac_room();
void close_clinic();
void close_vac_room();
char read_vac(int fd);
void error_exit();
void handler(int signal);
void lock_file(int fd);
void unlock_file(int fd);
void bring_vaccine(int id, int fd);
void vaccinate(int id, int cit_pipes[][2], int vac_pipes[][2]);
void freePtr(char *ptr);
void close_shm(char *sm_name, char *addr, int size);
char *open_shm(char *sm_name, int size);
int *open_int_shm(char *sm_name, int size, int initial);
void add_vac_buff(char vac);
void remove_vac_buff();
int check_new_double(char vac);
void close_int_shm(char *sm_name, int *addr, int size);
void close_other_pipes(int id, int pipes[][2], int size);
void close_read_pipes(int cit_pipes[][2], int size);
void close_write_pipes(int pipes[][2], int size);
int open_pipes(int cit_pipes[][2], int size);
void to_be_vac(int id, int cit_pipes[][2], int vac_pipes[][2]);
int choose_citizen();
int check_buff();
int str2int(char *str);
void int2str(int num, char str[]);
int main(int argc, char *argv[])
{

    int opt;
    memset(file_path, '\0', FILEPATHSIZE);
    while ((opt = getopt(argc, argv, "n:v:c:b:t:i:")) != -1)
    {
        switch (opt)
        {

        case 'n':

            if (optarg != NULL)
            {
                n_num = atoi(optarg);
            }

            break;

        case 'v':

            if (optarg != NULL)
            {

                v_num = atoi(optarg);
            }
            break;
        case 'c':

            if (optarg != NULL)
            {

                c_num = atoi(optarg);
            }
            break;
        case 'b':

            if (optarg != NULL)
            {

                b_size = atoi(optarg);
            }
            break;
        case 't':

            if (optarg != NULL)
            {

                t_num = atoi(optarg);
            }
            break;
        case 'i':

            if (optarg != NULL)
            {
                if (strlen(optarg) > 1024)
                {
                    printf("Too long file path \n");
                    exit(EXIT_FAILURE);
                }
                strcpy(file_path, optarg);
            }
            break;

        case '?':
            fprintf(stderr, "Exiting...\n");
            exit(EXIT_FAILURE);
            break;
        }
    }

    if ((n_num < 2) || (v_num < 2) || (c_num < 3) || (b_size < t_num * c_num + 1) || (t_num < 1) || (strlen(file_path) <= 0))
    {
        printf("Invalid argumant \n");
        exit(EXIT_FAILURE);
    }

    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
    int fd = open(file_path, O_RDWR, mode);
    if (fd == -1)
    {
        fprintf(stderr, "Error open file : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    pid_t n_pids[n_num];
    pid_t v_pids[v_num];

    living_child = n_num + v_num + c_num;

    cl_buff_arr = open_shm(CLINICBUFFARR, b_size);
    cit_vac_num = open_int_shm(CITIZENVACNUM, c_num, t_num);
    vac_dose_num = open_int_shm(DOSENUMBER, v_num, 0);
    if (cl_buff_arr == NULL || cit_vac_num == NULL || vac_dose_num == NULL)
        exit(EXIT_FAILURE);

    int cit_pipes[c_num][2]; // for inviting the citizen
    int vac_pipes[v_num][2]; // for continue to vaccinator
    if (open_pipes(cit_pipes, c_num) == -1)
        exit(EXIT_FAILURE);
    if (open_pipes(vac_pipes, v_num) == -1)
        exit(EXIT_FAILURE);

    sigset_t block_mask, empty_mask;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {

        fprintf(stderr, "sigaction : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGCHLD);
    if (sigprocmask(SIG_SETMASK, &block_mask, NULL) == -1)
    {

        fprintf(stderr, "sigprocmask : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    //----------------MAIN FIELD-------------

    create_clinic();
    create_vac_room();

    create_citizen(c_num, cit_pipes, vac_pipes);

    create_vaccinator(v_pids, v_num, cit_pipes, vac_pipes);
    create_nurse(fd, n_pids, n_num, cit_pipes, vac_pipes);
    close_other_pipes(-1, cit_pipes, c_num);
    close_other_pipes(-1, vac_pipes, v_num);
    //-----------------------------------------

    sigemptyset(&empty_mask);
    while (living_child > 0)
    {
        if (sigsuspend(&empty_mask) == -1 && errno != EINTR)
        {
            fprintf(stderr, "sigprocmask : %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
    }

    for (size_t i = 0; i < v_num * 2; i += 2)
    {
        printf("Vaccinator %ld (pid=%d) vaccinated %d doses.\n", i / 2 + 1, vac_dose_num[i], vac_dose_num[i + 1]);
    }
    printf("The clinic is now closed. Stay healthy.\n");
    close_clinic();
    close_vac_room();
    close_int_shm(CITIZENVACNUM, cit_vac_num, c_num);
    close_shm(CLINICBUFFARR, cl_buff_arr, b_size);
    close_int_shm(DOSENUMBER, vac_dose_num, v_num);
    close(fd);
    return 0;
}

/*
*   This function opens pipes
*/
int open_pipes(int pipes[][2], int size)
{

    for (size_t i = 0; i < size; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            fprintf(stderr, "Error open  pipe : %s\n", strerror(errno));
            return -1;
        }
    }

    return 0;
}

/*
*   This function close close unused pipes and write end of used pipes
*/
void close_other_pipes(int id, int pipes[][2], int size)
{

    for (size_t i = 0; i < size; i++)
    {

        if (i == id)
        {
            if (close(pipes[id][W]) == -1)
            {
                fprintf(stderr, "Error closing writing end of pipe : %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            if (close(pipes[i][W]) == -1)
            {
                fprintf(stderr, "Error closing writing end of pipe : %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            if (close(pipes[i][R]) == -1)
            {
                fprintf(stderr, "Error closing reading end of pipe : %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
        }
    }
}

/*
*   This funtion closes read end of the pipes
*/
void close_read_pipes(int pipes[][2], int size)
{

    for (size_t i = 0; i < size; i++)
    {

        if (close(pipes[i][R]) == -1)
        {
            fprintf(stderr, "Error closing reading end of pipe : %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
    }
}

/*
*   This function closes write end of the pipes
*/
void close_write_pipes(int pipes[][2], int size)
{

    for (size_t i = 0; i < size; i++)
    {

        if (close(pipes[i][W]) == -1)
        {
            fprintf(stderr, "Error closing reading end of pipe : %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
    }
}

/*
*   This function for citizens. They are vaccinated in this function
*/
void to_be_vac(int id, int cit_pipes[][2], int vac_pipes[][2])
{
    char buffer[PIPEBUFSIZE];
    int vacinator;
    for (size_t i = 0; i < t_num; i++)
    {
        memset(buffer, '\0', PIPEBUFSIZE);
        if (read(cit_pipes[id][0], buffer, PIPEBUFSIZE) == -1)
        {
            fprintf(stderr, "Error read pipe %d : %s\n", id, strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
        vacinator = str2int(buffer);

        if (sem_wait(&clinic->mutex) == -1)
        {
            fprintf(stderr, "Error wait full semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
        int f = clinic->f, s = clinic->s;
        if (sem_post(&clinic->mutex) == -1)
        {
            fprintf(stderr, "Error post empty semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }

        if (sem_wait(&vac_room->mutex) == -1)
        {
            fprintf(stderr, "Error wait full semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }

        cit_vac_num[id * 2 + 1] = t_num - (i + 1);
        if (i == t_num - 1)
        {
            vac_room->rem_c_num--;
            printf("Citizen %d (pid : %d) is vaccaniated for the %ld th time : the clinic has %d vaccine1 and %d vaccine2.The citizen is leaving. Remaining citizens to vaccinate: %d\n", id + 1, getpid(), i + 1, f, s, vac_room->rem_c_num);
            if (vac_room->rem_c_num == 0)
            {
                printf("All citizens have been vaccinated\n");
            }
        }
        else
        {
            printf("Citizen %d (pid : %d) is vaccaniated for the %ld th time: the clinic has %d vaccine1 and %d vaccine2.\n", id + 1, getpid(), i + 1, f, s);
        }

        if (sem_post(&vac_room->mutex) == -1)
        {
            fprintf(stderr, "Error post empty semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }

        memset(buffer, '\0', PIPEBUFSIZE);
        if (i != t_num - 1)
        {
            strcpy(buffer, "0");
        }
        else
            strcpy(buffer, "1");

        if (write(vac_pipes[vacinator][1], buffer, PIPEBUFSIZE) != PIPEBUFSIZE)
        {
            printf("Error writing to pipe 1.\n");
            _exit(1);
        }
    }
}

/*
*   This function creates nurses processes
*/
void create_nurse(int fd, pid_t n_pids[], int size, int cit_pipes[][2], int vac_pipes[][2])
{

    n_pids[0] = 12;

    for (size_t i = 0; i < size; i++)
    {

        switch (n_pids[i] = fork())
        {
        case -1:
            fprintf(stderr, "fork : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        case 0:
            close_other_pipes(-1, vac_pipes, v_num);
            close_other_pipes(-1, cit_pipes, c_num);
            bring_vaccine(i, fd);

            exit(EXIT_SUCCESS);
            break;
        default:

            break;
        }
    }
}

/*
*   This function creates vaccinators processes
*/
void create_vaccinator(pid_t v_pids[], int size, int cit_pipes[][2], int vac_pipes[][2])
{

    for (size_t i = 0; i < size; i++)
    {

        switch (v_pids[i] = fork())
        {
        case -1:
            fprintf(stderr, "fork : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        case 0:
            close_other_pipes(i, vac_pipes, v_num);
            close_read_pipes(cit_pipes, c_num);
            vaccinate(i, cit_pipes, vac_pipes);
            if (close(vac_pipes[i][R]) == -1)
            {
                fprintf(stderr, "Error closing writing end of pipe : %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            close_write_pipes(cit_pipes, c_num);
            exit(EXIT_SUCCESS);
            break;
        default:

            break;
        }
    }
}

/*
*   This function creates citizens processes
*/
void create_citizen(int size, int cit_pipes[][2], int vac_pipes[][2])
{
    for (size_t i = 0; i < size; i++)
    {

        switch (fork())
        {
        case -1:
            fprintf(stderr, "fork : %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        case 0:
            if (sem_wait(&vac_room->mutex) == -1)
            {
                fprintf(stderr, "Error wait full semaphore in clinic buffer: %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            cit_vac_num[i * 2] = getpid();

            if (sem_post(&vac_room->mutex) == -1)
            {
                fprintf(stderr, "Error post empty semaphore in clinic buffer: %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            close_read_pipes(vac_pipes, v_num);
            close_other_pipes(i, cit_pipes, c_num);
            to_be_vac(i, cit_pipes, vac_pipes);
            if (close(cit_pipes[i][R]) == -1)
            {
                fprintf(stderr, "Error closing writing end of pipe : %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            close_write_pipes(vac_pipes, v_num);
            exit(EXIT_SUCCESS);
            break;
        default:

            break;
        }
    }
    for (size_t i = 0; i < v_num; i++)
    {
        if (sem_post(&vac_room->start) == -1)
        {
            fprintf(stderr, "Error wait full semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
    }
}

/*
*   This function read vaccinies from the file and write in the buffer(producer)
*/
void bring_vaccine(int id, int fd)
{

    int flag = 0; // flag for there are 2 type vaccinie in the buffer
    char vac;
    while (1)
    {

        if (sem_wait(&clinic->empty) == -1)
        {
            fprintf(stderr, "Error wait mutex in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
        if (sem_wait(&clinic->mutex) == -1)
        {
            fprintf(stderr, "Error wait empty semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
        if (!clinic->is_end)
            while (1)
            {
                vac = read_vac(fd);
                if (vac != '\n')
                    break;
            }
        else
            vac = '0';

        if (vac != '1' && vac != '2') // if the vaccinies end
        {

            clinic->rem_nur_num--;
            if (clinic->rem_nur_num == 0)
            {
                printf("Nurses have carried all vaccines to the buffer, terminating.\n");
            }
            clinic->is_end = 1;
            if (sem_post(&clinic->mutex) == -1)
            {
                fprintf(stderr, "Error post mutex in clinic buffer: %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            if (sem_post(&clinic->empty) == -1)
            {
                fprintf(stderr, "Error post full semaphore in clinic buffer: %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            if (sem_post(&clinic->full) == -1)
            {
                fprintf(stderr, "Error post full semaphore in clinic buffer: %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            break;
        }

        flag = check_new_double(vac);
        add_vac_buff(vac);
        printf("Nurse %d (pid = %d) has brought vaccine vaccine %c : the clinic has %d vaccine1 and %d vaccine0\n", id + 1, getpid(), vac, clinic->f, clinic->s);

        if (sem_post(&clinic->mutex) == -1)
        {
            fprintf(stderr, "Error post mutex in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
        if (flag)
        {

            if (sem_post(&clinic->full) == -1)
            {
                fprintf(stderr, "Error post full semaphore in clinic buffer: %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
        }
    }
}

/*
*   This function checks is there any vaccine dual in the buffer
*/
int check_new_double(char vac)
{
    int before, after;
    if (clinic->f < clinic->s)
        before = clinic->f;
    else
        before = clinic->s;

    if (vac == '1')
        clinic->f++;
    else
        clinic->s++;

    if (clinic->f < clinic->s)
        after = clinic->f;
    else
        after = clinic->s;

    if (after > before)
        return 1;

    return 0;
}

/*
*   This function checks is there any vaccine in the buffer
*/
int check_buff()
{
    for (size_t i = 0; i < b_size; i++)
    {
        if (cl_buff_arr[i] == '1' || cl_buff_arr[i] == '2')
        {
            return 0;
        }
    }
    return 1;
}

/*
*   This function removes vaccines from the buffer
*/
void remove_vac_buff()
{
    int rem_1 = 0, rem_2 = 0;
    for (size_t i = 0; i < b_size; i++)
    {
        if (!rem_1 && cl_buff_arr[i] == '1')
        {
            cl_buff_arr[i] = '0';
            rem_1 = 1;
        }
        if (!rem_2 && cl_buff_arr[i] == '2')
        {
            cl_buff_arr[i] = '0';
            rem_2 = 1;
        }

        if (rem_1 && rem_2)
            break;
    }
}

/*
*   This function adds vaccine to the buffer
*/
void add_vac_buff(char vac)
{
    for (size_t i = 0; i < b_size; i++)
    {
        if (cl_buff_arr[i] == '0')
        {
            cl_buff_arr[i] = vac;
            break;
        }
    }
}

/*
*   This function for vaccinators. It takes vaccine dual from buffer and vaccinate the citizens
*/
void vaccinate(int id, int cit_pipes[][2], int vac_pipes[][2])
{

    if (sem_wait(&vac_room->start) == -1)
    {
        fprintf(stderr, "Error wait full semaphore in clinic buffer: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    int ct_id, flag = 1;
    while (1)
    {

        if (sem_wait(&clinic->full) == -1)
        {
            fprintf(stderr, "Error wait full semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }

        if (sem_wait(&clinic->mutex) == -1)
        {
            fprintf(stderr, "Error wait mutex in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
        if (flag)
        {
            vac_dose_num[id * 2] = getpid();
            flag = 0;
        }
        if (clinic->is_end && check_buff()) // if the all vaccinies finised
        {

            if (sem_post(&clinic->mutex) == -1)
            {
                fprintf(stderr, "Error post mutex in clinic buffer: %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            if (sem_post(&clinic->full) == -1)
            {
                fprintf(stderr, "Error post full semaphore in clinic buffer: %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            break;
        }

        remove_vac_buff();
        clinic->f--;
        clinic->s--;

        if (sem_post(&clinic->mutex) == -1)
        {
            fprintf(stderr, "Error post mutex in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }
        if (sem_post(&clinic->empty) == -1)
        {
            fprintf(stderr, "Error post empty semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }

        int sval;
        sem_getvalue(&vac_room->full, &sval);

        if (sem_wait(&vac_room->full) == -1)
        {
            fprintf(stderr, "Error wait full semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }

        if (sem_wait(&vac_room->mutex) == -1)
        {
            fprintf(stderr, "Error wait full semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }

        vac_dose_num[id * 2 + 1] = vac_dose_num[id * 2 + 1] + 1;

        ct_id = choose_citizen();
        if (ct_id == -1)
        {
            printf("????????????????????????????????????????All citizen has vacinate %d  \n", id);
            if (sem_post(&vac_room->mutex) == -1)
            {
                fprintf(stderr, "Error post empty semaphore in clinic buffer: %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
            break;
        }

        cit_vac_num[ct_id] = -1;

        printf("Vaccniator %d (pid : %d) inviting citizen pid : %d to the clinic \n", id + 1, getpid(), cit_vac_num[ct_id - 1]);
        if (sem_post(&vac_room->mutex) == -1)
        {
            fprintf(stderr, "Error post empty semaphore in clinic buffer: %s\n", strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }

        int pipe_id = ct_id / 2;
        char pipebuff[PIPEBUFSIZE];
        memset(pipebuff, '\0', PIPEBUFSIZE);
        int2str(id, pipebuff);
        if (write(cit_pipes[pipe_id][1], pipebuff, PIPEBUFSIZE) != PIPEBUFSIZE)
        {
            fprintf(stderr, "Error write pipe %d : %s\n", id, strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }

        memset(pipebuff, '\0', PIPEBUFSIZE);
        if (read(vac_pipes[id][0], pipebuff, PIPEBUFSIZE) == -1)
        {
            fprintf(stderr, "Error read pipe %d : %s\n", id, strerror(errno));
            error_exit();
            exit(EXIT_FAILURE);
        }

        if (pipebuff[0] == '0')
        {

            if (sem_post(&vac_room->full) == -1)
            {
                fprintf(stderr, "Error post empty semaphore in clinic buffer: %s\n", strerror(errno));
                error_exit();
                exit(EXIT_FAILURE);
            }
        }
    }
}

/*
*   This function converts string to integer
*/
int str2int(char *str)
{
    return atoi(str);
}

/*
*   This function converts integer to string
*/
void int2str(int num, char str[])
{

    snprintf(str, PIPEBUFSIZE, "%d", num);
}

/*
*   This function chooses available and oldest citizen from 'cit_vac_num' array for vaccinate
*/
int choose_citizen()
{
    for (size_t i = 1; i < c_num * 2; i += 2)
    {

        if (cit_vac_num[i] > 0)
            return i;
    }

    return -1;
}

/*
*   This function creates clinic shared memory
*/
void create_clinic()
{
    int fd = shm_open(CLINICSTORE, O_CREAT | O_RDWR, 0666);
    if (fd < 0)
    {
        fprintf(stderr, "Error opening clinic shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    if (ftruncate(fd, sizeof(CBuffer)) == -1)
    {
        fprintf(stderr, "Error resize clinic shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    clinic = mmap(NULL, sizeof(CBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (clinic == MAP_FAILED)
    {
        fprintf(stderr, "Error mmap clinic shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    if (sem_init(&clinic->mutex, 1, 1) == -1)
    {
        fprintf(stderr, "Error initialize clinic semaphore: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    if (sem_init(&clinic->empty, 1, b_size) == -1)
    {
        fprintf(stderr, "Error initialize clinic semaphore: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    if (sem_init(&clinic->full, 1, 0) == -1)
    {
        fprintf(stderr, "Error initialize clinic semaphore: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }

    clinic->f = 0;
    clinic->s = 0;
    clinic->size = 0;
    clinic->is_end = 0;
    clinic->rem_nur_num = n_num;
    close(fd);
}

/*
*   This function closes the clinic shared memory
*/
void close_clinic()
{

    if (munmap(clinic, sizeof(CBuffer)) == -1)
    {
        fprintf(stderr, "Error munmap shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    if (shm_unlink(CLINICSTORE) == -1)
    {
        fprintf(stderr, "Error unlink shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
}

/*
*   This function creates vaccine room shared memory
*/
void create_vac_room()
{
    int fd = shm_open(VACROOM, O_CREAT | O_RDWR, 0666);
    if (fd < 0)
    {
        fprintf(stderr, "Error opening vaccine room shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    if (ftruncate(fd, sizeof(VacRoom)) == -1)
    {
        fprintf(stderr, "Error resize vaccine room shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    vac_room = mmap(NULL, sizeof(VacRoom), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (vac_room == MAP_FAILED)
    {
        fprintf(stderr, "Error mmap vaccine room shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    if (sem_init(&vac_room->mutex, 1, 1) == -1)
    {
        fprintf(stderr, "Error initialize vaccine room semaphore: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    if (sem_init(&vac_room->full, 1, c_num) == -1)
    {
        fprintf(stderr, "Error initialize vaccine room semaphore: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    if (sem_init(&vac_room->start, 1, 0) == -1)
    {
        fprintf(stderr, "Error initialize vaccine room semaphore: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    vac_room->rem_c_num = c_num;
    close(fd);
}

/*
*   This function closes vaccine room shared memory
*/
void close_vac_room()
{
    if (munmap(vac_room, sizeof(VacRoom)) == -1)
    {
        fprintf(stderr, "Error munmap shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
    if (shm_unlink(VACROOM) == -1)
    {
        fprintf(stderr, "Error unlink shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
}

/*
*   This function locks the file
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

        fprintf(stderr, "Lock file : %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
}

/*
*   This function unlocks the file
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

        fprintf(stderr, "Unlock file : %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
}

/*
*   This function reads 1 vaccine
*/
char read_vac(int fd)
{
    lock_file(fd);
    char c[0];
    c[0] = '\0';
    read(fd, c, 1);

    unlock_file(fd);
    return c[0];
}

/*
*   This function sends SIGINT signal to other processes if there is any error
*/
void error_exit()
{
    printf("Program exit because of the error\n");
    killpg(0, SIGINT);
}

/*
*   This is SIGCHLD signal hanler
*/
void handler(int signal)
{

    int savedErrno = errno;

    /*
    *   This part taken from page 557 of the textbook
    */
    if (signal == SIGCHLD)
    {
        int status;
        pid_t cpid;

        while ((cpid = waitpid(-1, &status, WNOHANG)) > 0)
            living_child--;

        if (cpid == -1 && errno != ECHILD)
        {

            fprintf(stderr, "Waitpid error: %s\n", strerror(errno));
            error_exit();
        }
    }

    errno = savedErrno;
}

/*
*   This function opens shared memory 
*/
char *open_shm(char *sm_name, int size)
{
    char *addr;
    int smd = shm_open(sm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (smd == -1)
    {

        fprintf(stderr, "Error opening shared memory: %s\n", strerror(errno));
        return NULL;
    }

    if (ftruncate(smd, size) == -1)
    {
        fprintf(stderr, "Error resize shared memory: %s\n", strerror(errno));
        return NULL;
    }

    addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, smd, 0);
    if (addr == MAP_FAILED)
    {
        fprintf(stderr, "Error map shared memory: %s\n", strerror(errno));
        return NULL;
    }

    memset(addr, '0', size);

    close(smd);
    return addr;
}

/*
*   This function opens integer shared memory 
*/
int *open_int_shm(char *sm_name, int size, int initial)
{
    int *addr;
    int smd = shm_open(sm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (smd == -1)
    {

        fprintf(stderr, "Error opening shared memory: %s\n", strerror(errno));
        return NULL;
    }

    if (ftruncate(smd, size * 2 * sizeof(int)) == -1)
    {
        fprintf(stderr, "Error resize shared memory: %s\n", strerror(errno));
        return NULL;
    }

    addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, smd, 0);
    if (addr == MAP_FAILED)
    {
        fprintf(stderr, "Error map shared memory: %s\n", strerror(errno));
        return NULL;
    }

    for (size_t i = 0; i < 2 * size; i++)
    {
        addr[i] = initial;
    }

    close(smd);
    return addr;
}

/*
*   This function closes shared memory 
*/
void close_shm(char *sm_name, char *addr, int size)
{
    if (munmap(addr, size) == -1)
    {
        fprintf(stderr, "Error munmap shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }

    if (shm_unlink(sm_name) == -1)
    {
        fprintf(stderr, "Error unlinking shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
}

/*
*   This function closes integer shared memory 
*/
void close_int_shm(char *sm_name, int *addr, int size)
{
    if (munmap(addr, size * 2 * sizeof(int)) == -1)
    {
        fprintf(stderr, "Error munmap shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }

    if (shm_unlink(sm_name) == -1)
    {
        fprintf(stderr, "Error unlinking shared memory: %s\n", strerror(errno));
        error_exit();
        exit(EXIT_FAILURE);
    }
}

/*
*   This function free the char pointers
*/
void freePtr(char *ptr)
{
    if (ptr != NULL)
        free(ptr);
}
