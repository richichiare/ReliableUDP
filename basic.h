#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include "list.h"

#define SERV_PORT 5171

#define failure_prob 0.5

#define MAXLINE 1024

#define SLIDING_WND	10

#define MAX_PAYLOAD 1483
#define min_seq  (int) pow(2.0,8.0)
#define max_seq  (int) pow(2.0,31.0)

#define max_connid  (int) pow(2.0,8.0)

extern int snd_base , next_seqnum , rcv_base;

extern char *name;
extern int tot_child ;
extern int len_clients , cmd_flag , conn_flag , is_dyn, get_already_done, put_already_done, list_already_done ;
extern struct node_t *head;
extern char *content;
extern long ext_rtt , dev_rtt ;
extern long timeout ;
extern struct node_t *client_list ;

struct header{

	unsigned int seq_tid;
	unsigned int ack_connid;
	unsigned char flags; // SYN + RST  + FIN + ACK + RSU + CMD + CMD_MSG(2bit)
	unsigned char payload[MAX_PAYLOAD];
};

struct pkt_wndw{
	struct header *pkt;
	unsigned char ack;//0 if not acknowledged; 1 if already acknowledged
	int time_fd;
	long dyn_timer;
	unsigned char jumped;
};

struct thread_data {
	int frame;
	int child;
	int fd;
	unsigned int seq, ack;
	pthread_t tid;
	unsigned char cmd_flag;
};

int get_sockfd(void);
void send_to(int , struct header , struct sockaddr_in);
void do_bind(int , struct sockaddr_in *);
fd_set do_select(fd_set , int , struct timeval *);
fd_set do_select_block(fd_set , int );
void read_from_skt(struct sockaddr_in *, int , struct header *);
void write_to_fd(int , unsigned char *, ssize_t );
void write_on_filelist(char *);
size_t get_file_length(int );
int  open_file(const  char *);
fd_set snd_pkt_in_wndw(int, struct pkt_wndw **, struct sockaddr_in , int , int , fd_set *);
void re_send(int , struct pkt_wndw **, int, struct sockaddr_in , int ,int , fd_set *);
void slide_wndw(struct pkt_wndw *);
void slide_wndw_rcv(/*int *base,*/struct pkt_wndw *);
struct header build_pkt(unsigned int , unsigned int , unsigned char , char *);
struct header build_pkt_file(unsigned int , unsigned int , unsigned char , unsigned char *, int );
void ack_reciver(struct pkt_wndw *,char *,unsigned int , unsigned char *,int );
void ack_sender(struct pkt_wndw *,unsigned int, fd_set *);
void *get_pkt_size(char *, int *);
void *get_numpkt(char *, int *, char **, int *);
void child_job( struct header, struct sockaddr_in );
struct header check_isConnection(struct header);
void start_connection( struct header * , struct header * , int , pid_t , struct sockaddr_in * );
void read_from_pipe(int *, unsigned char *, ssize_t);
void child_job_client(int *, int );
void close_connection(unsigned int , struct sockaddr_in, fd_set *);
int mylock(int, int);