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
#include <arpa/inet.h>

#define ID_SIZE 10

#define QUERY_SIZE 2048
#define NAME_LEN 64

typedef struct queue
{
	char *query;
	struct queue *next;
	struct queue *rear;

} queue;

void handler(int sig);
void error_exit();
void read_file(char *file_path);
void empty_queue();
void enqueue(char *query);
char *dequeue();
void free_src(void *source);

sig_atomic_t end_flag = 0;
int id = -1;
int port = -1;
int socket1 = -1;
queue *query_queue;
queue *queue_tail;

void print_log(char *str);

int main(int argc, char *argv[])
{
	signal(SIGINT, handler);
	char ip[NAME_LEN];
	char query_file[NAME_LEN];
	memset(ip, '\0', NAME_LEN);
	memset(query_file, '\0', NAME_LEN);
	int opt;
	while ((opt = getopt(argc, argv, "i:a:p:o:")) != -1)
	{
		switch (opt)
		{

		case 'i':

			if (optarg != NULL)
			{
				id = atoi(optarg);
			}

			break;
		case 'a':

			if (optarg != NULL)
			{
				if (strlen(optarg) > NAME_LEN)
				{
					printf("Too long ip \n");
					exit(EXIT_FAILURE);
				}
				strcpy(ip, optarg);
			}
			break;
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
				strcpy(query_file, optarg);
			}
			break;

		case '?':
			fprintf(stderr, "Exiting...\n");
			exit(EXIT_FAILURE);
			break;
		}
	}

	if (id < 1 || strlen(ip) == 0 || port < 0 || strlen(query_file) == 0)
	{
		printf("Invalid input\nExiting...\n");
		exit(EXIT_FAILURE);
	}

	char log_buff[QUERY_SIZE];
	memset(log_buff, '\0', QUERY_SIZE);
	sprintf(log_buff, "Client-%d connecting to %s:%d", id, ip, port);
	print_log(log_buff);

	read_file(query_file);

	struct sockaddr_in serv_addr;
	char buffer[QUERY_SIZE];
	memset(buffer, '\0', QUERY_SIZE);
	if ((socket1 = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "Error to socket creation : %s\n", strerror(errno));
		error_exit();
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	
	if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) < 0)
	{
		fprintf(stderr, "Error to convert ip address file : %s\n", strerror(errno));
		error_exit();
	}

	if (connect(socket1, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		fprintf(stderr, "Error to socket connection : %s\n", strerror(errno));
		error_exit();
	}
	char *query = NULL;
	char buff[QUERY_SIZE];
	int rec_num = 0;
	while (1)
	{
		memset(buff, '\0', QUERY_SIZE);
		query = dequeue();
		if (query == NULL || end_flag)
		{

			free_src(query);
			strcpy(buff, "exit");
			if (write(socket1, buff, QUERY_SIZE) == -1)
			{
				fprintf(stderr, "Error to write socket : %s\n", strerror(errno));
				error_exit();
			}
			break;
		}
		strcpy(buff, query);
		sprintf(log_buff, "Client-%d connected and sending query '%s'", id, query);
		print_log(log_buff);
		free_src(query);
		clock_t begin = clock();
		if (write(socket1, buff, QUERY_SIZE) == -1)
		{
			fprintf(stderr, "Error to write socket : %s\n", strerror(errno));
			error_exit();
		}
		memset(buff, '\0', QUERY_SIZE);
		if (read(socket1, buffer, QUERY_SIZE) == -1)
		{
			fprintf(stderr, "Error to read socket : %s\n", strerror(errno));
			error_exit();
		}
		int size = atoi(buffer);
		clock_t end = clock();
		double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
		sprintf(log_buff, "Server's response to Client-%d is %d records, and arrived in %.1f seconds", id, size, time_spent);
		print_log(log_buff);
		if (size == 0)
		{
			printf("There is no data !!! \n");
		}
		else
		{
			for (size_t i = 0; i < size; i++)
			{
				memset(buff, '\0', QUERY_SIZE);

				if (read(socket1, buffer, QUERY_SIZE) == -1)
				{
					fprintf(stderr, "Error to read socket : %s\n", strerror(errno));
					error_exit();
				}
				printf("%s", buffer);
			}
		}
		rec_num++;
	}

	sprintf(log_buff, "A total of %d queries executed, client is terminating", rec_num);
	print_log(log_buff);

	empty_queue();
	close(socket1);
	return 0;
}

/**
 * 	This function prints log to screen
*/
void print_log(char *str)
{
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
	printf("%s", log_data);
}

/**
 * 	This function frees the source
*/
void free_src(void *source)
{
	if (source == NULL)
		return;
	free(source);
}

/**
 *  This function empties the queue
*/
void empty_queue()
{
	while (1)
	{
		char *str = dequeue();
		if (str == NULL)
			break;
		free_src(str);
	}
}

/**
 *  This function adds new element to queue
*/
void enqueue(char *query)
{

	if (query_queue == NULL)
	{
		query_queue = (queue *)malloc(sizeof(queue));
		queue_tail = query_queue;
		query_queue->query = (char *)malloc(strlen(query) + 1);
		memset(query_queue->query, '\0', strlen(query) + 1);
		strcpy(query_queue->query, query);
		query_queue->next = NULL;
		query_queue->rear = NULL;
	}
	else
	{
		queue *temp = (queue *)malloc(sizeof(queue));
		temp->query = (char *)malloc(strlen(query) + 1);
		;
		memset(temp->query, '\0', strlen(query) + 1);
		strcpy(temp->query, query);
		temp->next = query_queue;
		temp->rear = NULL;
		query_queue->rear = temp;
		query_queue = temp;
	}
}

/**
 *  This function removes element from queue
*/
char *dequeue()
{

	if (query_queue == NULL)
	{
		return NULL;
	}
	else if (queue_tail == query_queue)
	{
		char *str = (char *)malloc(strlen(query_queue->query) + 1);
		memset(str, '\0', strlen(query_queue->query) + 1);
		strcpy(str, query_queue->query);
		free_src(query_queue->query);
		free_src(query_queue);
		query_queue = NULL;
		queue_tail = NULL;
		return str;
	}
	else
	{
		char *str = (char *)malloc(strlen(queue_tail->query) + 1);
		memset(str, '\0', strlen(queue_tail->query) + 1);
		strcpy(str, queue_tail->query);
		free_src(queue_tail->query);
		queue_tail = queue_tail->rear;
		free_src(queue_tail->next);
		return str;
	}
}

/**
 * 	This function reads the query file
*/
void read_file(char *file_path)
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
	int fl = 0, fl2 = 0, fl3 = 0;
	char query_buff[QUERY_SIZE];
	char num_buf[ID_SIZE];
	memset(num_buf, '\0', ID_SIZE);
	memset(query_buff, '\0', QUERY_SIZE);
	int i1 = 0, i2 = 0;
	do
	{

		bytes_read = read(fd, c, 1);
		offset += bytes_read;
		if (fl3 && c[0] == '\n')
			fl3 = 0;

		else if (!fl3)
		{
			if (c[0] != ' ' && !fl2)
			{
				fl = 1;
				num_buf[i1] = c[0];
				i1++;
			}
			if (fl && c[0] == ' ')
			{
				//	printf("num : -%s- \n",num_buf);
				int t_id = atoi(num_buf);
				if (id == t_id)
					fl2 = 1;
				else
					fl3 = 1;
				i1 = 0;
				fl = 0;
				memset(num_buf, '\0', ID_SIZE);
			}

			if (fl2)
			{
				if (c[0] == '\n')
				{
					fl2 = 0;
					i2 = 0;
					enqueue(query_buff);
					//printf("my : -%s- \n", query_buff);
					memset(query_buff, '\0', QUERY_SIZE);
				}
				else
				{
					query_buff[i2] = c[0];
					i2++;
				}
			}
		}

	} while (offset < file_length);

	close(fd);
}

/**
 *  Error exit function
*/
void error_exit()
{
	if (socket1 > 0)
		close(socket1);
	empty_queue();
	exit(EXIT_FAILURE);
}

/**
 *  This is SIGINT signal handler
*/
void handler(int sig)
{
	
	int savedErrno = errno;
	printf("SIGINT signal received\nProgram will terminate\n");
	end_flag = 1;
	errno = savedErrno;

	return;
}
