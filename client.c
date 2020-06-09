#include "list.h"
#include "basic.h"
#include "timer.h"
#include "tree.h"
#include "command.h"


//DEFINIZIONE DELLE VARIABILI

int snd_base = 0, next_seqnum = 0, rcv_base=0;  //Variabili per la connessione
struct sockaddr_in newaddr_client;
char cmd[24]; //Buffer contenente quello che viene scritto dall'utente
unsigned int conn_seq;
struct node_t *head = NULL;
int sockfd_client, cmd_flag = 0;
long ext_rtt = 1, dev_rtt = 1;
long timeout = 100000000;

int is_dyn = 1; //Variabile per impostare il timeout dinamico

//Buffer trasferimento affidabile

struct pkt_wndw *put_buffer_client;
struct pkt_wndw *child_buff_client;
struct pkt_wndw *cmd_pkt_client;
char *file_name_toget; //File-name nel caso di comando get



void insert_value(unsigned char v, struct node_t **phead)
{
	struct node_t *new;

	new = alloc_node();
	new->value = v;
	insert_after_node(new, &head);

}

//Funzione che si occupa di inviare un FIN
void close_connection(unsigned int seq, struct sockaddr_in addr, fd_set *fd_timer){

	struct header pkt = build_pkt(seq, 1, 32, "\0");//Costruzione del pacchetto di SYN
	cmd_pkt_client = malloc(sizeof(struct pkt_wndw));
	cmd_pkt_client[0].pkt = malloc(sizeof(struct header));
	*(cmd_pkt_client[0].pkt) = pkt;
	cmd_pkt_client[0].ack = 0;
	cmd_pkt_client[0].time_fd = -1;
	cmd_pkt_client[0].jumped = 0;
	snd_pkt_in_wndw(sockfd_client, &cmd_pkt_client, addr, 1, 1, fd_timer);  //Invio pacchetto di SYN
	next_seqnum = 0;
	cmd_flag = 1;
}


//funzione che legge dal payload ricevuto il numero di pacchetti il nome del file e la dimensione dell'ultimo pacchetto
void *get_numpkt_client(char *s, int *numpkt, int *last_pkt_size){

	char *msg, *p;
	int i = 0;

	errno = 0;
	*numpkt = strtoul (s , &msg , 10); //Tramite la funzione strtoul posso ricavare il numero dei pacchetti che si trovano all'inizio della stringa s,in msg ho il resto della stringa
	if (errno != 0){
		fprintf ( stderr ,
		" Invalid number in standard input line \n") ;
		return NULL;
	}

	errno = 0;
	*last_pkt_size = strtoul(msg+ 1, &p, 10); //In last_pkt_size ho la dimensione dell'ultimo pacchetto(che può essere minore di MAXPAYLOAD)
	if (errno != 0){
		fprintf ( stderr ,
		" Invalid number in standard input line \n") ;
		return NULL;
	}
}


	
void child_job_client(int pfd[2], int end_cmd_pipe) {


	//Definzione variabili
	struct header pkt;
	pid_t pid;
	int time_fd[2], numpkt, flags, die = 0;
	char path[1024];
	fd_set fd_timer;
	//Inizializzazione struct per non rendere bloccante la select
	struct timeval tm; 
	tm.tv_sec = 0;
	tm.tv_usec = 0;
	fd_set fd_check;
	int numb_pkt, last_pkt_size, open_already_done =0, list_already_done = 0, free_already_done = 0;
	time_t close_timer;
	int pkt_remaining;
	unsigned int ack, seq_cmd, fin_seq_tid;

	//Inizializzazione dati per la select
	FD_ZERO(&fd_timer);
	FD_ZERO(&fd_check);
	FD_SET(pfd[0], &fd_timer);  //Aggiunta pipe nella select
	for(;;)
	{

		fd_check = do_select(fd_timer, FD_SETSIZE, &tm);  
		usleep(100);
		if(FD_ISSET(pfd[0], &fd_check))  //Alla ricezione di dati dalla pipe
		{

			read_from_pipe(pfd, (unsigned char *) &(pkt.seq_tid), sizeof(unsigned int));// seq_tid
			
			read_from_pipe(pfd, (unsigned char *) &(pkt.ack_connid) , sizeof(unsigned int)); // ack_connid
		
			read_from_pipe(pfd, (unsigned char *) &pkt.flags , sizeof(unsigned char)); //flags
		
			read_from_pipe(pfd, (unsigned char *) &pkt.payload, sizeof(pkt.payload)); //payload
	
			read_from_pipe(pfd, (unsigned char *) &newaddr_client, sizeof(newaddr_client)); //Indirizzo socket client

			read_from_pipe(pfd, (unsigned char *) &ack, sizeof(ack)); //Ack per i prossimi pacchetti

		}

		//Ricezione pacchetti di comando
		if((pkt.ack_connid == 1) && (pkt.flags != 0) && !die) 
		{
			next_seqnum = 0;
			snd_base = 0;
			rcv_base = 0;
			open_already_done = 0;
			list_already_done = 0;
			free_already_done = 0;

			switch(pkt.flags)
			{
				//Ricezione list
				case 1:
					do_client_list(pkt.seq_tid, ack, newaddr_client, &fd_timer);
					break;
				//Ricezione get
				case 2:
					do_client_get(pkt.seq_tid, ack, newaddr_client, &fd_timer);
					break;
				//Ricezione put
				case 3:
					numpkt = do_client_put(pkt.seq_tid, ack, newaddr_client, &fd_timer);
					if(numpkt == -1){
						write_to_fd(end_cmd_pipe, "Terminated", strlen("Terminated"));
						break;
					}
					break;
				//Ricezione FIN
				case 32:
					fin_seq_tid = ((pkt.seq_tid >> 8) +numpkt +1) << 8;
					close_connection(fin_seq_tid, newaddr_client, &fd_timer);
					die = 1;
					break;
			}
			seq_cmd = pkt.seq_tid; //Salvataggio numero di sequenza
			memset((void*) &pkt, 0, sizeof(struct header));
		}

		//Controllo scadenza timer
		if(cmd_flag == 1){ //Controllo i timer di cmd_flag
			if(cmd_pkt_client[0].time_fd != -1){
				//Se il timer scade effettuo il rinvio
				if(FD_ISSET(cmd_pkt_client[0].time_fd, &fd_check)){
					re_send(sockfd_client, &cmd_pkt_client, 0, newaddr_client, 1, 1, &fd_timer);
				}
			}
		}else{
			//Controllo se sono scaduti i timer dei pacchetti presenti nella finestra
			for(int i=snd_base; i < (snd_base + SLIDING_WND) && (i < numpkt); i++){ 	
				if(put_buffer_client[i].time_fd != -1){
					if(FD_ISSET(put_buffer_client[i].time_fd, &fd_check)){
						re_send(sockfd_client, &put_buffer_client, i, newaddr_client, SLIDING_WND, numpkt, &fd_timer);
					}
				}
			}
		}

		//Riscontro ack del fin
		if((pkt.flags == 48) && (pkt.ack_connid == fin_seq_tid))
		{
			if(cmd_pkt_client[0].time_fd != -1){
				FD_CLR(cmd_pkt_client[0].time_fd, &fd_timer);
				if(close(cmd_pkt_client[0].time_fd) == -1){
					perror("close fd cmd");
					exit(-1);
				}
			}	

			cmd_flag = -1; //perche ho riscontrato il pacchetto di fin
			seq_cmd = -1;
			cmd_pkt_client[0].time_fd = -1;
			memset((void*) &pkt, 0, sizeof(struct header));
		}

		//Ricevo il fin del server
		if((pkt.flags == 32)){

			//Costruzione pacchetto di FINACK

			struct header ack_fin;
			ack_fin.seq_tid = ((fin_seq_tid >> 8) + 1) << 8;
			ack_fin.ack_connid = pkt.seq_tid;
			ack_fin.flags = 48; //FIN-ACK
			srand((unsigned)(time(NULL)));
			double success = (rand() % 10000) / 10000.0;
			//Invio del pacchetto in modo probabilistico
			if(success > failure_prob){
				send_to(sockfd_client, ack_fin, newaddr_client); //Invio l'utlimo ack
			}
			close_timer = time(NULL);
			die = 2; 
			memset((void*) &pkt, 0, sizeof(struct header));
		}

		//Attesa temporizzata, dopo tale attesa uccido il processo client
		if(((time(NULL) - close_timer) > 5) && (die == 2)){
			printf("Connection closed!\n");
			if(kill(getppid(), SIGUSR1) != 0){
				perror("kill");
				exit(EXIT_FAILURE);
			}
			exit(EXIT_SUCCESS);
		}

		//Gestione caso in cui il file già esista in una PUT
		if((pkt.flags == 67)){

			if(cmd_pkt_client[0].time_fd != -1)
			{
				printf("%s\n", pkt.payload);
				write_to_fd(end_cmd_pipe, "Terminated", strlen("Terminated"));  //Comunicazione al padre tramite pipe,in modo che possa riattivare lo standard input
				FD_CLR(cmd_pkt_client[0].time_fd, &fd_timer);
				if(close(cmd_pkt_client[0].time_fd) == -1){
					perror("close fd cmd");
					exit(-1);
				}
			}	
			cmd_flag = 0;
			seq_cmd = -1;
			cmd_pkt_client[0].time_fd=-1;
			memset((void*) &pkt, 0, sizeof(struct header));
		}


		//Server mi ha inviato l'ack della richiesta di get con il numero di pacchetti e dimensione dell'ultimo pacchetto
		if(((pkt.flags == 18) || (pkt.flags == 66)) && (pkt.ack_connid == seq_cmd))
		{

			if(pkt.flags == 66)
			{  //Errore file get

				if(cmd_pkt_client[0].time_fd != -1)
				{
					printf("%s\n", pkt.payload);
					write_to_fd(end_cmd_pipe, "Terminated", strlen("Terminated")); //Comunicazione al padre tramite pipe,in modo che possa riattivare lo standard input
					FD_CLR(cmd_pkt_client[0].time_fd, &fd_timer);
					if(close(cmd_pkt_client[0].time_fd) == -1)
					{
						perror("close fd cmd");
						exit(-1);
					}
				}	
				cmd_flag = 0;
				seq_cmd = -1;
				cmd_pkt_client[0].time_fd=-1;
				memset((void*) &pkt, 0, sizeof(struct header));

			}
			else
			{

				get_numpkt_client(pkt.payload, &numb_pkt, &last_pkt_size); //Acquisizione numero di pacchetti e dimensione dell'ultimo pacchetto
	
				//Costruzione buffer per la finestra di ricezione
				child_buff_client = malloc(numb_pkt * sizeof(struct pkt_wndw));
				for(int x=0; x<numb_pkt; x++) 
				{
					struct header *packet=malloc(sizeof(struct header));
					(child_buff_client+x)->pkt=packet;
					if(x == numb_pkt - 1){
						sprintf((child_buff_client+x)->pkt->payload, "%d", last_pkt_size); //Nel payload inserisco la dimensione dell'ultimo pkt
	
					}else
					{
						sprintf((child_buff_client+x)->pkt->payload, "%d", MAX_PAYLOAD);
					}
	
					unsigned int seq= ((pkt.seq_tid >> 8) +x+1) << 8;
					(child_buff_client+x)->pkt->seq_tid=seq;
				}

				//Invio ACKGET
				struct header ack_get;
				ack_get.seq_tid = ((pkt.ack_connid >> 8) +1) << 8;
				ack_get.ack_connid = pkt.seq_tid;
				ack_get.flags = 18;
				srand((unsigned)(time(NULL)));
				double success = (rand() % 10000) / 10000.0;
				if(success > failure_prob){
					send_to(sockfd_client, ack_get, newaddr_client); //Invio l'utlimo pacchetto dell'"handshake" prima di inviare il file
				}
				cmd_flag = 0; //perche ho riscontrato il pacchetto di get
				seq_cmd = -1;
			}
		}

		//Server mi ha inviato l'ack della richiesta di list con il numero di pacchetti
		if((pkt.flags == 17) && (pkt.ack_connid == seq_cmd)){

			char *file_name;
			get_numpkt_client(pkt.payload, &numb_pkt, &last_pkt_size);
			//Costruisco il buffer con gli ack che penso di ricevere

			child_buff_client = malloc(numb_pkt * sizeof(struct pkt_wndw));
			for(int x=0; x<numb_pkt; x++) {
				struct header *packet=malloc(sizeof(struct header));

				(child_buff_client+x)->pkt=packet;
				memset((void *) (child_buff_client+x)->pkt->payload, 0, MAX_PAYLOAD);

				if(x == numb_pkt - 1){
					sprintf((child_buff_client+x)->pkt->payload, "%d", last_pkt_size);

				}else{
					sprintf((child_buff_client+x)->pkt->payload, "%d", MAX_PAYLOAD);
				}
					
				unsigned int seq= ((pkt.seq_tid >> 8) +x+1) << 8;
				(child_buff_client+x)->pkt->seq_tid=seq;
			}

			struct header ack_get;
			ack_get.seq_tid = ((pkt.ack_connid >> 8) +1) << 8;
			ack_get.ack_connid = pkt.seq_tid;
			ack_get.flags = 17;

			srand((unsigned)(time(NULL)));
			double success = (rand() % 10000) / 10000.0;
			if(success > failure_prob){
				send_to(sockfd_client, ack_get, newaddr_client); //Invio l'utlimo pacchetto dell'"handshake" prima di inviare il file
			}
			cmd_flag = 0; //perche ho riscontrato il pacchetto di get
			seq_cmd = -1;
		}
		
		//Riscontro PUT pkt
		if((pkt.flags == 19) && (pkt.ack_connid == seq_cmd))
		{

			if(cmd_pkt_client[0].time_fd != -1){
				FD_CLR(cmd_pkt_client[0].time_fd, &fd_timer);
				if(close(cmd_pkt_client[0].time_fd) == -1){
					perror("close fd cmd");
					exit(-1);
				}
			}	
			snd_pkt_in_wndw(sockfd_client, &put_buffer_client,newaddr_client,SLIDING_WND, numpkt, &fd_timer);
			cmd_flag = 0;
			seq_cmd = -1;
		}



		//Riscontro pacchetti di PUT
		if(((pkt.flags == 19) || (pkt.flags == 27)) && (pkt.ack_connid != 1) && (pkt.ack_connid != seq_cmd)){

			if(snd_base != numpkt){

				ack_sender(put_buffer_client,pkt.ack_connid, &fd_timer); //RIscontro pacchetti
				snd_pkt_in_wndw(sockfd_client, &put_buffer_client,newaddr_client,SLIDING_WND, numpkt, &fd_timer);

			}
			//RIcevuti tutti i dati
			if(snd_base == numpkt && !free_already_done)
			{
				free(put_buffer_client);
				free(cmd_pkt_client);
				FD_ZERO(&fd_timer);
				FD_SET(pfd[0], &fd_timer);
				write_to_fd(end_cmd_pipe, "Terminated", strlen("Terminated"));
				free_already_done = 1;
			}

			if(pkt.flags == 27 ){
				fprintf(stdout, "%s\n", pkt.payload);
			}

			memset((void*) &pkt, 0, sizeof(struct header));
		}

		char *content_data = malloc(1);
		
		//Riscontro pacchetti GET

		if(((pkt.flags == 2) || (pkt.flags == 10)) && (pkt.ack_connid != 1) && (pkt.ack_connid != seq_cmd)){
			
			if(open_already_done !=1){

				ack_reciver(child_buff_client, content_data, pkt.seq_tid, pkt.payload, numb_pkt); //Sposto la finestra

				for(int x=0;x<numb_pkt;x++)
				{	
				if(!child_buff_client[x].ack){
					break;
				}
				if((x==numb_pkt-1) && !open_already_done) //Ultimo pacchetto
				{
					int fd=open(file_name_toget , O_TRUNC | O_CREAT | O_RDWR, 0777);
					if(fd==-1)
					{
						perror("open");
						exit(-1);
					}
					memset((void*) &path, 0, sizeof(path));
					open_already_done = 1;
					//Scrittura sul file
					for(int i=0;i<numb_pkt;i++)
					{
						if(i == numb_pkt-1){
							write_to_fd(fd, child_buff_client[i].pkt->payload, last_pkt_size);
						}else{
							write_to_fd(fd, child_buff_client[i].pkt->payload, MAX_PAYLOAD);
						}
					}
					if(rcv_base == numb_pkt){
						free(child_buff_client);
						free(cmd_pkt_client);

					}
					
				}
			}

			}

			struct header ack_pkt;

			if(pkt.flags == 10){
				ack_pkt=build_pkt(((pkt.ack_connid >> 8) + 1) << 8, pkt.seq_tid, 26, "\0");
				printf("%s\n", pkt.payload);  
				write_to_fd(end_cmd_pipe, "Terminated", strlen("Terminated"));
			}else{
				ack_pkt=build_pkt(((pkt.ack_connid >> 8) + 1) << 8, pkt.seq_tid, 18, "\0");
			}
			srand((unsigned)(time(NULL)));
			double success = (rand() % 10000) / 10000.0;
			if(success > failure_prob){
				send_to(sockfd_client, ack_pkt, newaddr_client);
			}
		
			memset((void*) &pkt, 0, sizeof(struct header));		
		}

		//Ricezione ack dei pacchetti list 
		if(((pkt.flags == 1) || (pkt.flags == 9)) && (pkt.ack_connid != 1) && (pkt.ack_connid != seq_cmd)){

			if(!list_already_done)
			{

				ack_reciver(child_buff_client, content_data, pkt.seq_tid, pkt.payload, numb_pkt);  //Riscontro pacchetti sul buffer di ricezione 
				if(pkt.flags == 1)
				{
					for(int x=0;x<numb_pkt;x++)
					{
						if(!child_buff_client[x].ack)
						{  //Controlla se ho riscontrato tutti i pacchetti
							break;
						}
						printf("Available files are: \n");
						printf("\n");
						if(x==numb_pkt-1 && !list_already_done) 
						{
							for(int i=0;i<numb_pkt;i++ )
							{
								if(i == numb_pkt-1)
								{
									write_to_fd(STDOUT_FILENO, child_buff_client[i].pkt->payload, last_pkt_size);
								}else{
									write_to_fd(STDOUT_FILENO, child_buff_client[i].pkt->payload, MAX_PAYLOAD);
								}
							}

							list_already_done=1;

							if(rcv_base == numb_pkt){
								free(child_buff_client);
								free(cmd_pkt_client);
							}
					
						}
					}
					printf("\n");

				}
			}
			
			struct header ack_pkt;

			if(pkt.flags == 9 ){
				ack_pkt=build_pkt(((pkt.ack_connid >> 8) + 1) << 8, pkt.seq_tid, 25, "\0");
				printf("%s\n", pkt.payload);
				write_to_fd(end_cmd_pipe, "Terminated", strlen("Terminated")); //Comunica al padre di riattivare lo stdin
			}else{
				ack_pkt=build_pkt(((pkt.ack_connid >> 8) + 1) << 8, pkt.seq_tid, 1, "\0"); //Costruisci pacchetto di list
			}

			srand((unsigned)(time(NULL)));
			double success = (rand() % 10000) / 10000.0;
			if(success > failure_prob){
				send_to(sockfd_client, ack_pkt, newaddr_client);
			}
		
			memset((void*) &pkt, 0, sizeof(struct header));

				
		}

	}
}

//Handler chiamato alla ricezione di segnale della morte del figlio
void exit_handler(int sig){
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv){

	//DIchiarazione delle variabili

	pid_t pid, process_id;
	int pfd[2], fla, end_cmd_pipe[2], stdin_activated = 0;//0:reading; 1:writing
	unsigned char *buf;
	int status, close_fd;
	//Struct per non rendere bloccante la select
	struct timeval tm; 
	tm.tv_sec = 0;
	tm.tv_usec = 0;

	struct sockaddr_in servaddr;
	fd_set fd_array, fd_check;
	struct header pkt_built, pkt_rcv;
	struct pkt_wndw check_pkt;
	struct header pkt0, pkt1;

	//Errore in caso in cui l'utente non inserisce l'indirizzo ip del client
	if(argc < 2){
		fprintf(stderr, "Request IP address\n");
		exit(-1);
	}

	sockfd_client = get_sockfd();
	FD_ZERO(&fd_array);
	FD_ZERO(&fd_check);

	//Creo la struttura per fare la sendto, recvfrom ecc..
	memset((void *)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(SERV_PORT);
	if(inet_pton(AF_INET, argv[1],  &servaddr.sin_addr) <= 0){
		perror("inet_pton");
		exit(-1);
	}

	//Apertura pipe
	if ((pipe(pfd) == -1) || (pipe(end_cmd_pipe) == -1)) {
		perror("Error in pipe()\n");
		exit(-1);
	}

	//Creazione del figlio tramite preforking
	if((pid = fork()) == -1){
		perror("Error in fork()\n");
		exit(-1);
	}

	if(pid == 0){
		if((close(pfd[1]) == -1) || (close(end_cmd_pipe[0]) == -1)){  //Chiusura lato pipe non usato
			perror("close");
		}
		child_job_client(pfd, end_cmd_pipe[1]);
	}else{
		process_id = pid;
	}

	if((close(pfd[0]) == -1) || (close(end_cmd_pipe[1]) == -1)){
		perror("close");
	}

	struct pkt_wndw *connection_buff = malloc(2 * sizeof(struct pkt_wndw));//Allocazione buffer per la connessione affidabile
	struct itimerspec *curr_value;
	unsigned int ACK_mask = 0xFFFFFF00;

	//Generazione pacchetto di SYN
	
	srand((unsigned)time(NULL));
	conn_seq = (rand() % (max_seq - min_seq) + min_seq) << 8;//Random seq. num.
	unsigned int conn_ack = 0;
	unsigned char flags = 128;//SYN flag
	pkt_built = build_pkt(conn_seq, conn_ack, flags, "\0");
	connection_buff->pkt = malloc(sizeof(struct header));
	*(connection_buff)->pkt = pkt_built;//NOTA: impostare il timer
	//Inizializzazione secondo pacchetto della finestra(ACK)
	connection_buff[1].pkt = malloc(sizeof(struct header));
	connection_buff[1].time_fd = connection_buff[0].time_fd  = -1;
	connection_buff[1].jumped = connection_buff[0].jumped  = 0;
	snd_pkt_in_wndw(sockfd_client, &connection_buff, servaddr, 1, 2, &fd_array); //Invia il primo pacchetto
	FD_SET(end_cmd_pipe[0], &fd_array); //Inserimento della pipe per comunicare col figlio in ascolto sulla select

	//CHiusura connessione nel caso il server non risponda dopo molto tempo
	if(close_fd != 0)
	{	
		//Cancellazione vecchio timer e inizio del nuovo
		FD_CLR(close_fd, &fd_array);
		if(close(close_fd) == -1)
		{
			perror("close");
			exit(EXIT_FAILURE);
		}
		close_fd = closure_timer(&fd_array);
	}else{
		close_fd = closure_timer(&fd_array);
	}

	for(;;){

		//Alla ricezione del segnale del figlio uccidi anche il processo padre
		if(signal(SIGUSR1, exit_handler) == SIG_ERR){
			perror("signal");
			exit(EXIT_FAILURE);
		}
	
		FD_SET(sockfd_client, &fd_array);//Aggiunta socket sulla select
		fd_check = do_select(fd_array, FD_SETSIZE, &tm);

		//Controllo la scadenza del timer del primo pacchetto

		if(connection_buff[0].time_fd != -1)
		{
			if(FD_ISSET(connection_buff[0].time_fd, &fd_check))
			{
				re_send(sockfd_client, &connection_buff, 0, servaddr, 1, 2, &fd_array);
			}	
		}

		//COntrollo la scadenza del secondo timer
		if(connection_buff[1].time_fd != -1){
			if(FD_ISSET(connection_buff[1].time_fd, &fd_check)){
				re_send(sockfd_client, &connection_buff, 1, newaddr_client, 1, 2, &fd_array);
			}	
		}

		if(FD_ISSET(end_cmd_pipe[0], &fd_check)){
			//Re-insert stdin
			FD_SET(STDIN_FILENO, &fd_array);
		}

		//Chiusura connessione per inattività
		if(FD_ISSET(close_fd, &fd_check)){
			fprintf(stderr, "%s\n", "Connection timed out! Try to re-connect!");
			exit(EXIT_FAILURE);
		}


		if(FD_ISSET(sockfd_client, &fd_check)){
			
			memset((void *)&newaddr_client, 0, sizeof(newaddr_client));
			socklen_t len = sizeof(newaddr_client);
			read_from_skt(&newaddr_client, sockfd_client, &pkt_rcv); //Lettura dalla socket

			//Ricezione di un SYNACK
			if(pkt_rcv.flags == 144){
 
				unsigned int ack_serv = pkt_rcv.ack_connid; //Salvo ack_connection necessario successivmnete
				ack_sender(connection_buff,ack_serv, &fd_array); //Riscontro pacchetto nella finestra del mittente
				//Costruzione pkt di ACK
				conn_seq = (((ack_serv & ACK_mask) >> 8) +1) << 8;
				conn_ack = pkt_rcv.seq_tid;
				unsigned char flags = 16; 
				pkt_built = build_pkt(conn_seq, conn_ack, flags, "\0");//Building ACK pkt
				(connection_buff+1)->pkt = malloc(sizeof(struct header));
				*(connection_buff+1)->pkt = pkt_built;
				srand((unsigned)(time(NULL)));
				double success = (rand() % 10000) / 10000.0;
				if(success > failure_prob){
						send_to(sockfd_client, *(connection_buff+1)->pkt, newaddr_client);
				}

				printf("%s\n","Ok, you are now connected!");
				printf("\n");
				printf("%s\n","Insert a command! Possible commands are: LIST, GET, PUT");
				if(!stdin_activated){
						FD_SET(STDIN_FILENO, &fd_array); //Attivo lo standard input sulla select dopo aver stabilito la connessione
						stdin_activated = 1;  //Imposto a uno,in modo che,se mi arrivano altri SYN_ACK non rimetto la standard input nella select
				}
			}
			

			//Essendo la select resa non bloccante,comunico col figlio solo alla ricezione dei pacchetti con i seguenti flag
			if((((pkt_rcv.flags == 3) || (pkt_rcv.flags == 9) || (pkt_rcv.flags == 6) || (pkt_rcv.flags == 5) || (pkt_rcv.flags == 18) || (pkt_rcv.flags == 17) || (pkt_rcv.flags == 7) || (pkt_rcv.flags == 19) || (pkt_rcv.flags == 66) || (pkt_rcv.flags == 2) || (pkt_rcv.flags == 1) || (pkt_rcv.flags == 67) || (pkt_rcv.flags == 27) || (pkt_rcv.flags == 32) || (pkt_rcv.flags == 10) || (pkt_rcv.flags == 48)))){

				//Timer da impostare per gestire l'assenza del server
				if(close_fd != 0){
					FD_CLR(close_fd, &fd_array);
					if(close(close_fd) == -1){
						perror("close");
						exit(EXIT_FAILURE);
					}
					close_fd = closure_timer(&fd_array);
				}else{
					close_fd = closure_timer(&fd_array);
				}

				//Comunicazione col figlio sulla pipe
				unsigned int a = 0;
				write_to_fd(pfd[1], (unsigned char *) &(pkt_rcv.seq_tid) , sizeof(unsigned int));// seq_tid
				write_to_fd(pfd[1], (unsigned char *) &(pkt_rcv.ack_connid), sizeof(unsigned int)); // ack_connid
				write_to_fd(pfd[1], (unsigned char *) &(pkt_rcv.flags), sizeof(unsigned char)); //flags
				write_to_fd(pfd[1], (unsigned char *) &pkt_rcv.payload, sizeof(pkt_rcv.payload)); //payload
				write_to_fd(pfd[1], (unsigned char *) &newaddr_client, sizeof(newaddr_client)); //Indirizzo cleint
				write_to_fd(pfd[1], (unsigned char *) &a, sizeof(a));  //Ack da inviare
			}
		}

	
		
		//Gestione standard input
		if(FD_ISSET(STDIN_FILENO, &fd_check)){
			
			scanf("%s",cmd); //Prendo il comando dallo standard input
			conn_ack = (((conn_ack & ACK_mask) >> 8) + 1);
			//Passo il controllo al figlio
			if(check_cmd(cmd, pfd[1], conn_seq, conn_ack) != -1){
				FD_CLR(STDIN_FILENO,&fd_array);
			}
		}

	}
}

