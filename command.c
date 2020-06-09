#include "command.h"
#include "basic.h"

char *file_name_toget; //FIle name usato per il comando get
int sockfd_client;
struct sockaddr_in newaddr_client_client;



//Funzione che si occupa di leggere il contenuto di un file
void read_block(int in, char *buf, int size)
{
	int r;
	ssize_t v;
	r = 0;
	
	while (size > r) {
		v = read(in, buf, size - r);

		if (v == -1) {
			fprintf(stderr, "Error while reading file\n");
			exit(EXIT_FAILURE);
		}
		if (v == 0)
			return;
		r += v;
		buf += v;
	}
}

//Funzione che si occupa della frammentazione del file
void fragment(int fd,int numb_pkt,int len,unsigned int seq, unsigned int ack)
{

	int v = lseek(fd , 0,SEEK_END); //v contiene la dimensione del file
	lseek(fd, 0, SEEK_SET);
	unsigned char *buf = malloc(v);
	memset((void *) buf, 0, sizeof(*buf));
	read_block(fd, buf, v);
	unsigned int seq_mask = 0xFFFFFF00;
	unsigned int n_seq = seq & seq_mask;
	unsigned int n_ack = ack & seq_mask;
	struct header pkt;
	
	for (int i = 0; i < numb_pkt; i++)
	{
		n_seq = ((seq >> 8) + i +1) << 8;
		n_ack = ((ack >> 8) + i +1) << 8;

		if(i == numb_pkt - 1){
			if(v%MAX_PAYLOAD!=0)
			{			
				pkt=build_pkt_file(n_seq, n_ack, 3, buf, (v%MAX_PAYLOAD));  //Il contenuto dell'ultimo pacchetto è diverso da MAX_PAYLOAD
			}
			else
			{
				pkt=build_pkt_file(n_seq, n_ack, 3, buf, MAX_PAYLOAD);
			}
			
		}else{
			pkt=build_pkt_file(n_seq, n_ack, 3, buf, MAX_PAYLOAD); 
			buf = buf + (MAX_PAYLOAD); //Sposto il buffer di MAX_PAYLOAD
		}
		
		//Costruzione dati da inviare nella finestra
		(put_buffer_client+i)->pkt = malloc(sizeof(struct header));
		memset((void *) (put_buffer_client+i)->pkt->payload, 0, MAX_PAYLOAD);
		*(put_buffer_client+i)->pkt = pkt;
		put_buffer_client[i].time_fd=-1;
		put_buffer_client[i].ack=0;
		put_buffer_client[i].jumped = 0;
	}

}

//Funzione che permette l'invio del messaggio di get
int do_client_get(unsigned int seq, unsigned int ack,struct sockaddr_in addr, fd_set *fd_timer){

	file_name_toget = malloc(100 * sizeof(char));
	memset((void *) file_name_toget, 0, 100);
	int n;
	//Input file_name
	do{
		printf("Insert filename:\n");
		n=scanf("%s",file_name_toget);
		if(n==0){
			printf("Not valid input\n");
		}
	}while(n==0);

	struct header pkt=build_pkt(seq, 1, 6, file_name_toget);//Costruzione pacchetto di richiesta get
	cmd_pkt_client = malloc(sizeof(struct pkt_wndw));
	cmd_pkt_client[0].pkt = malloc(sizeof(struct header));
	*(cmd_pkt_client[0].pkt) = pkt;
	cmd_pkt_client[0].ack = 0;
	cmd_pkt_client[0].time_fd = -1;
	cmd_pkt_client[0].jumped = 0;
	snd_pkt_in_wndw(sockfd_client, &cmd_pkt_client, addr, 1, 1, fd_timer); //Invio pacchetto in modo affidabile
	printf("Sending..\n");
	next_seqnum = 0;
	cmd_flag = 1;
}


int do_client_put(unsigned int seq, unsigned int ack,struct sockaddr_in addr, fd_set *fd_timer)
{
	char *path = malloc(100 * sizeof(char));
	memset((void *) path, 0, sizeof(100));

	int n,numb_pkt;
	unsigned char *b;

	//Input path
	do{
		printf("Insert filename:\n");
		n=scanf("%s",path);
		if(n==0)
			printf("Not valid input\n");
	}while(n==0);

	int fd=open_file(path);
	if(fd == -1)
		return -1;
	int len = get_file_length(fd); //Len contiene la dimensione del file
	int rest = len%MAX_PAYLOAD; //rest contiene il resto di len e MAX_PAYLOAD,questo è importante per definire il numero di pacchetti

	if(rest)
		numb_pkt=(int)(len/MAX_PAYLOAD)+1;
	else
		numb_pkt=(int)(len/MAX_PAYLOAD);

	unsigned char *buf = malloc(sizeof(int) + (strlen(path) * sizeof(char)));
	memset((void *) buf, 0, (sizeof(int) + (strlen(path) * sizeof(char))));
	char *space_rest = malloc(1 + sizeof(rest));
	memset((void *) space_rest, 0, (1 + sizeof(rest)));
	//Costruzione payload da inviare
	sprintf(buf,"%d",numb_pkt);
	strcat(buf, path);
	sprintf(space_rest, " %d", rest);
	strcat(buf, space_rest);
	free(path);
	struct header pkt=build_pkt(seq, 1, 7, buf);//Special put pkt
	free(buf);
	unsigned int seq_mask = 0xFFFFFF00;
	unsigned int seq_num = seq & seq_mask;
	cmd_pkt_client = malloc(sizeof(struct pkt_wndw));
	put_buffer_client = malloc(sizeof(struct pkt_wndw) * numb_pkt); //Allocazione struttura contenete i pacchetti da inviare
	for (int i = 0; i < numb_pkt; ++i){
		(put_buffer_client+i)->pkt = malloc(sizeof(struct header)); //Allocazione pkt per ogni pacchetto da inviare
		memset((void *) ((put_buffer_client+i)->pkt->payload), 0, MAX_PAYLOAD);
	}
	cmd_pkt_client[0].pkt = malloc(sizeof(struct header));
	*(cmd_pkt_client[0].pkt) = pkt;
	cmd_pkt_client[0].ack = 0;
	cmd_pkt_client[0].time_fd = -1;
	cmd_pkt_client[0].jumped = 0;
	snd_pkt_in_wndw(sockfd_client, &cmd_pkt_client, addr, 1, 1, fd_timer); //Invio del pacchetto in maniera affidabile
	printf("Sending..\n");
	next_seqnum = 0;
	cmd_flag = 1;
	fragment(fd,numb_pkt,len, seq, ack); //Frammentazione file
	return numb_pkt;
}


//Funzione che invia pacchetto di richiesta list
int do_client_list(unsigned int seq, unsigned int ack, struct sockaddr_in addr, fd_set *fd_timer){

	struct header pkt = build_pkt(seq, 1, 5, "\0");//Costruzione pacchetto di List
	cmd_pkt_client = malloc(sizeof(struct pkt_wndw));
	cmd_pkt_client[0].pkt = malloc(sizeof(struct header));
	*(cmd_pkt_client[0].pkt) = pkt;
	cmd_pkt_client[0].ack = 0;
	cmd_pkt_client[0].time_fd = -1;
	cmd_pkt_client[0].jumped = 0;
	snd_pkt_in_wndw(sockfd_client, &cmd_pkt_client, addr, 1, 1, fd_timer); //Invio pacchetto in maniera affidabile
	printf("Sending..\n");
	next_seqnum = 0;
	cmd_flag = 1;
}



void call_command(unsigned char cmd,int fd, unsigned int seq_num, unsigned int ack_num)
{

	struct header pkt=build_pkt((((seq_num >> 8) + 1) << 8), 1, cmd, "\0"); //Costruzione pacchetto 
	write_to_fd(fd, (unsigned char *) &(pkt.seq_tid) , sizeof(unsigned int));// seq_tid
	write_to_fd(fd, (unsigned char *) &(pkt.ack_connid), sizeof(unsigned int)); // ack_connid
	write_to_fd(fd, (unsigned char *) &(pkt.flags), sizeof(unsigned char)); //flags
	write_to_fd(fd, (unsigned char *) &pkt.payload, sizeof(pkt.payload)); //payload
	write_to_fd(fd, (unsigned char *) &newaddr_client, sizeof(newaddr_client)); //Struttura client
	write_to_fd(fd, (unsigned char *) &ack_num, sizeof(ack_num)); //Ack number
}	

//Controllo inserimento comandi
int check_cmd(char *cmd, int fd, unsigned int seq_num, unsigned int ack_num){

	
	for(int x=0;x<strlen(cmd);x++)
		cmd[x]=(char)toupper(cmd[x]); //Permetto di accettare sia comandi in corsivo che in maiuscolo
	
	if(strcmp(cmd,"LIST")==0)
		call_command((unsigned char)1,fd, seq_num, ack_num);  
	else if(strcmp(cmd,"GET")==0)
		call_command((unsigned char)2,fd, seq_num, ack_num);
	else if(strcmp(cmd,"PUT")==0){
		call_command((unsigned char)3, fd, seq_num, ack_num);
	}else if(strcmp(cmd,"END")==0){
		call_command((unsigned char)32, fd, seq_num, ack_num);
	}
	else{
		fprintf(stderr,"Not valid command! Try with put, get or list.\n"); //Errore quando inserisco un comando errato
		return -1;
	}
	
}
