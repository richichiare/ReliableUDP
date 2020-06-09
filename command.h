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

extern struct pkt_wndw *put_buffer_client;
extern struct pkt_wndw *child_buff_client;
extern struct pkt_wndw *cmd_pkt_client;
extern int sockfd_client;
extern struct sockaddr_in newaddr_client;
extern char *file_name_toget;

void read_block(int , char *, int);
void fragment(int ,int ,int ,unsigned int , unsigned int);
int do_client_get(unsigned int, unsigned int ,struct sockaddr_in , fd_set *);
int do_client_put(unsigned int , unsigned int ,struct sockaddr_in , fd_set *);
int do_client_list(unsigned int , unsigned int , struct sockaddr_in, fd_set *);
void call_command(unsigned char,int, unsigned int , unsigned int );
int check_cmd(char *, int, unsigned int, unsigned int);
