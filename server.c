#include "basic.h"
#include "list.h"
#include "timer.h"
#include "tree.h"

//DEFINIZIONE DELLE VARIABILI

//Variabili necessarie per il trasferimento affidabile
int snd_base = 0, next_seqnum = 0, rcv_base=0;  
struct pkt_wndw *child_buff;
struct pkt_wndw *cmd_pkt;
struct pkt_wndw *cmd_buffer;

time_t prova;

int is_dyn = 1; //Variabile per il timer dinamico

//Flag necessari per correggere situazioni di errore
int cmd_flag = -1, conn_flag = 0, get_already_done = 0, put_already_done = 0, list_already_done = 0;
struct node_t *head = NULL;
long ext_rtt = 1, dev_rtt = 1, timeout = 5; //Variabili per la gestione del timer adattativo
struct node_t *client_list = NULL;
char *content;


//Funzione per inserire un valore nella lista dinamica
void insert_value(unsigned char v, struct node_t **phead)
{
	struct node_t *new;

	new = alloc_node();
	new->value = v;
	insert_after_node(new, &head);

}

//Funzione che effettua la lettura del file
void read_block(int in, char *buf, int size)
{
	int r;
	ssize_t v;
	r = 0;
	
	while (size > r) 
	{
		v = read(in, buf, size - r);

		if (v == -1) 
		{
			fprintf(stderr, "Error while reading file\n");
			exit(EXIT_FAILURE);
		}
		if (v == 0)
			return;
		r += v;
		buf += v;
	}
}

//Funzione necessaria per effettuare la frammentazione del file in pacchetti
void fragment(int fd,int numb_pkt,int len,unsigned int seq, unsigned int ack, unsigned int flags)
{

	int dim = lseek(fd , 0,SEEK_END); //In dim vi è la dimensione del file
	lseek(fd, 0, SEEK_SET);
	unsigned char *buf = malloc(dim);
	memset((void *) buf, 0, sizeof(*buf));
	read_block(fd, buf, dim);  //In buf inserisco il contenuto del file
	unsigned int seq_mask = 0xFFFFFF00;
	unsigned int n_seq = seq & seq_mask;
	unsigned int n_ack = ack & seq_mask;
	struct header pkt;
	//Procedura di frammentazione
	for (int i = 0; i < numb_pkt; i++)
	{
		n_seq = ((seq >> 8) + i +1) << 8;  //Numero di sequenza per ogni pacchetto i
		n_ack = ((ack >> 8) + i +1) << 8;  //Numero di ack per ogni pacchetto i
		//L'ultimo pacchetto potrebbe avere una dimensione minore del payload,per tale motivo è necessario distinguere i due casi
		if(i == numb_pkt - 1)
		{
			if(dim% MAX_PAYLOAD!=0)
				pkt=build_pkt_file(n_seq, n_ack, flags, buf, (dim % MAX_PAYLOAD));
			else
				pkt=build_pkt_file(n_seq, n_ack, flags, buf, MAX_PAYLOAD);
		}
		else
		{
			pkt=build_pkt_file(n_seq, n_ack, flags, buf, MAX_PAYLOAD); 
			buf = buf + (MAX_PAYLOAD); //Ad ogni iterazione spostiamo il buffer di MAX_PAYLOAD per costruire il pacchetto i+1
		}
		(cmd_buffer+i)->pkt = malloc(sizeof(struct header));
		memset((void *) (cmd_buffer+i)->pkt->payload, 0, MAX_PAYLOAD);
		//Assegnamento dei dati ad ogni elemento del buffer
		*(cmd_buffer+i)->pkt = pkt;
		cmd_buffer[i].time_fd=-1;
		cmd_buffer[i].ack=0;
		cmd_buffer[i].jumped = 0;
	}
	if(close(fd) == -1){
		perror("close");
		exit(EXIT_FAILURE);
	}
}

//Funzione che si occupa di rispondere a un messaggio di get ricevuto dal client
int do_Sget(struct header pktr, int sockfd, struct sockaddr_in addr, unsigned int seqnum, unsigned int ack, node *root, char **path, fd_set *fd_timer)
{

	//Definizione delle variabili
	int n,numb_pkt,fd;
	size_t filesize;
	unsigned char * mem;	
	fd_set set, return_set;
	struct header pkt_snd , pkt_rcv;
	cmd_flag = 0;
	memset((void *) path,0,sizeof(path));
	*path = malloc(1024 * sizeof(char));
	//Ricerca del file attraverso la struttura ad albero
	sprintf(*path, "%s","doc/");
	search(&root, pktr.payload, *path);
	strcat(*path, pktr.payload);
	errno = 0;
	fd = open_file(*path);
	//Controllo sull'esistenza del file richiesto
	if(errno !=0 && errno == ENOENT)
	{
		printf("File does not exist\n");
		return -1;
	}

	if(mylock(fd, F_RDLCK) == -1){
		perror("mylock in get");
		exit(EXIT_FAILURE);
	}

	//Calcolo del numero dei pacchetti
	filesize = get_file_length(fd);
	int rest = filesize%MAX_PAYLOAD;                 												
	if(rest)                                          /*Se la dimensione del file non è un multiplo di MaXPAYLOAD,*/
		numb_pkt=(int) ( filesize/MAX_PAYLOAD ) + 1;  /*necessitiamo di un pacchetto in più contenenete meno di MAXPAYLOAD byte*/
	else
		numb_pkt  =(int)(filesize/MAX_PAYLOAD);

	//Costruzione pacchetto di ack al get
	char *buf = malloc(20 * sizeof(char));
	sprintf(buf,"%d",numb_pkt);
	char *space_rest = malloc(sizeof(rest));
	sprintf(space_rest, " %d", rest);
	strcat(buf, space_rest);
	ack = ((ack >> 8) + 2) << 8;
	seqnum = (((seqnum >> 8) + 1) << 8);
	struct header pkt=build_pkt(seqnum, ack, 18,buf);
	free(buf);
	cmd_buffer = malloc(sizeof(struct pkt_wndw) * numb_pkt);
	cmd_pkt = malloc(sizeof(struct pkt_wndw));
	cmd_pkt[0].pkt = malloc(sizeof(struct header));
	*(cmd_pkt[0].pkt) = pkt;
	cmd_pkt[0].ack = 0;
	cmd_pkt[0].time_fd = -1;
	cmd_pkt[0].jumped = 0;
	snd_pkt_in_wndw(sockfd, &cmd_pkt, addr, 1, 1, fd_timer);  //Invio pacchetto in modo affidabile
	next_seqnum = 0;
	cmd_flag = 1;
	fragment(fd, numb_pkt, filesize, seqnum, ack, (unsigned char) 2);  //Frammentazione file
	return numb_pkt;

}                    

//Funzione che si occupa di rispondere a un messaggio di list ricevuto dal client
int do_Slist(struct header pktr, int sockfd, struct sockaddr_in addr, unsigned int seqnum, unsigned int ack, fd_set *fd_timer)
{

	//Definizione variabili
	int n,numb_pkt,fd;
	unsigned char * mem;	
	fd_set set, return_set;
	struct header pkt_snd , pkt_rcv;
	cmd_flag = 0;
	errno = 0;
	fd = open_file("doc/filelist.txt");
	//Controllo se il filelist non è presente
	if(errno !=0 && errno == ENOENT)
	{
		printf("File does not exist\n");
		return -1;
	}
	//Calcolo del numero di pacchetti
	int filesize = get_file_length(fd);
	int rest = filesize % MAX_PAYLOAD;
	if(rest)												/*Se la dimensione del file non è un multiplo di MaXPAYLOAD,*/
		numb_pkt = (int) ( filesize/MAX_PAYLOAD ) + 1;		/*necessitiamo di un pacchetto in più contenenete meno di MAXPAYLOAD byte*/
	else
		numb_pkt = (int) (filesize/MAX_PAYLOAD);
	//Costruzione pacchetto di risposta a cmd list
	char buf[20];
	sprintf(buf,"%d",numb_pkt);
	char *space_rest = malloc(sizeof(rest));
	sprintf(space_rest, " %d", rest);
	strcat(buf, space_rest);
	ack = ((ack >> 8) + 2) << 8;
	seqnum = (((seqnum >> 8) + 1) << 8);
	struct header pkt=build_pkt(seqnum, ack, 17,buf);
	cmd_buffer = malloc(sizeof(struct pkt_wndw) * numb_pkt);
	cmd_pkt = malloc(sizeof(struct pkt_wndw));
	cmd_pkt[0].pkt = malloc(sizeof(struct header));
	*(cmd_pkt[0].pkt) = pkt;
	cmd_pkt[0].ack = 0;
	cmd_pkt[0].time_fd = -1;
	cmd_pkt[0].jumped = 0;
	snd_pkt_in_wndw(sockfd, &cmd_pkt, addr, 1, 1, fd_timer); //Invio pacchetto in modo affidabile

	prova = time(NULL);

	next_seqnum = 0;
	cmd_flag = 1;
	//Frammentazione file
	fragment(fd, numb_pkt, filesize, seqnum, ack, (unsigned char) 1);
	return numb_pkt;
}                              


//Funzione eseguita da ogni processo figlio(ogni processo figlio corrisponde a un client diverso)
void child_job( struct header pkt, struct sockaddr_in client_info)
{

	//Definizione delle variabili
	struct header pktr;
	struct sockaddr_in child_addr; //Struttura del client
	fd_set set, return_set;
	int numb_pkt;
	unsigned int seq_cmd, ack_cmd, ack_get, fin_ack_seq,seq_mask = 0xFFFFFF00;
	char *file_name;
	int last_pkt_size, pkt_remaining, fd_opened = 0, fin_recived = 0, write_already_done = 0, close_fd = 0;
	time_t close_timer, time_last_ack;

	//Struct per non rendere bloccante la select
	struct timeval tm; 
	tm.tv_sec = 0;
	tm.tv_usec = 0;

	//Costruzione dell'albero binario
	node *root; //radice dell'albero
	root = NULL;
	char *path = malloc(1024 * sizeof(char *));
	strcat(path, "doc/");
	insert(&root, "lmn");
    insert(&root, "fgh");
    insert(&root, "cde");
    insert(&root, "ijk");
    insert(&root, "ab");
    insert(&root, "rst");
    insert(&root, "opq");
    insert(&root, "uvw");
    insert(&root, "xyz");
    //Inizializzazione delle struct del client e bind della socket
	memset((void *)&child_addr,0,sizeof(child_addr));
	child_addr.sin_family=AF_INET;
	child_addr.sin_addr.s_addr=htonl(INADDR_ANY);
	child_addr.sin_port=htons(0);
	int sockfd_client = get_sockfd();
	if(bind(sockfd_client,(struct sockaddr *)&child_addr,sizeof(child_addr))<0)
	{
		perror("errore in bind");
		exit(-1);
	}
	FD_ZERO(&set);
	FD_ZERO(&return_set);
	FD_SET(sockfd_client, &set); //Aggiunta della socket in ascolto alla socket
	int l = 0;
	for(;;)
	{

		//Ricezione del SYN_ACK dal padre
		if(pkt.flags == 144)
		{

			cmd_pkt = malloc(sizeof(struct pkt_wndw));
			cmd_pkt[0].pkt = malloc(sizeof(struct header));
			*(cmd_pkt[0].pkt) = pkt;
			cmd_pkt[0].ack = 0;
			cmd_pkt[0].time_fd = -1;
			cmd_pkt[0].jumped = 0;
			conn_flag = 1;
			snd_pkt_in_wndw(sockfd_client, &cmd_pkt, client_info, 1, 1, &set);//Invio pkt di SYN-ACK
			close_fd = closure_timer(&set); //Avvio di un timer,per chiudere la connessione col client nel caso in cui resti inattivo per troppo tempo 
			next_seqnum = 0;
			seq_cmd = pkt.seq_tid; //Salvati per incrementarli per i prossimi pacchetti da inviare
			ack_cmd = pkt.ack_connid;
			memset((void*) &pkt, 0, sizeof(struct header));
		}
		return_set = do_select(set, FD_SETSIZE, &tm); //Inzializzazione della select di tipo non bloccante
		//usleep(100);
		//Controllo scadenza di ogni timer,distinguendo il caso in cui abbiamo il cmd_pkt e il cmd_buffer
		if(cmd_flag == 1 || conn_flag == 1) //Quando uno dei due flag è impostato a 1,viene usato il buffer cmd_pkt
		{ 
			if(cmd_pkt[0].time_fd != -1)
			{
				if(FD_ISSET(cmd_pkt[0].time_fd, &return_set)) //Se un timer scade avviene il rinvio del pacchetto
				{ 
					re_send(sockfd_client, &cmd_pkt, 0, client_info, 1, 1, &set);
				}
			}
		}else if(cmd_flag == 0) //Se il flag cmd_flag è impostato a 0,si controlla la scadenza di un pacchetto conentente dati di un file
		{ 
				for(int i=snd_base; i < (snd_base + SLIDING_WND) && (i < numb_pkt); i++)
				{ 	
					if(cmd_buffer[i].time_fd != -1)
					{
						if(FD_ISSET(cmd_buffer[i].time_fd, &return_set))
						{
							re_send(sockfd_client, &cmd_buffer, i, client_info, SLIDING_WND, numb_pkt, &set);
						}
					}
				}
		}

		//Invio del pacchetto di FIN dopo 3 secondi
		if((((time(NULL) - close_timer) > 3) && (fin_recived == 1)))
		{

			struct header pkt = build_pkt(((fin_ack_seq >> 8) +1) << 8, 1, 32, "\0");
			cmd_pkt[0].pkt = malloc(sizeof(struct header));
			*(cmd_pkt[0].pkt) = pkt;
			cmd_pkt[0].ack = 0;
			cmd_pkt[0].time_fd = -1;
			cmd_pkt[0].jumped = 0;
			snd_pkt_in_wndw(sockfd_client, &cmd_pkt, client_info, 1, 1, &set);
			next_seqnum = 0;
			cmd_flag = 1;
			fin_recived = 0; 
		}

		//Dopo un pò di tempo che il client non invia più dati il processo muore
		if(FD_ISSET(close_fd, &return_set)) 
		{
			printf("Connection close for inactivity! Just dying..\n");
			if(kill(getppid(), SIGUSR1) != 0)
			{
				perror("kill");
				exit(EXIT_FAILURE);
			}
			exit(EXIT_SUCCESS);
		}
		if(FD_ISSET(sockfd_client, &return_set)) 
		{

			read_from_skt(&client_info, sockfd_client, &pktr); //Ascolto sulla socket
			//Ricezione ACK
			if(pktr.flags == 16)
			{
				unsigned int ack_client = pktr.ack_connid;
				if((ack_client >> 8) == (seq_cmd >> 8))
				{
					printf("Connected with IP: %s and port: %u\n", inet_ntoa(client_info.sin_addr) , client_info.sin_port);
					if(cmd_pkt[0].time_fd != -1)
					{
						FD_CLR(cmd_pkt[0].time_fd, &set); //Rimozione del timer sul pacchetto
						if(close(cmd_pkt[0].time_fd) == -1)
						{
							perror("close fd cmd");
							exit(-1);
						}
					}
					conn_flag = 0;
					cmd_pkt[0].time_fd = -1;
					//IMpostazione nuovo timer per chiudere la connessione in caso il cient sia inattivo per troppo tempo
					if(close_fd != 0)
					{
						FD_CLR(close_fd, &set);
						if(close(close_fd) == -1)
						{
							perror("close");
							exit(EXIT_FAILURE);
						}
						close_fd = closure_timer(&set); 
					}else{
						close_fd = closure_timer(&set);
					}
					memset((void*) &pktr, 0, sizeof(struct header)); 
				}
			}

			//Ricezione pacchetto di get
			if((pktr.flags == 6) && (pktr.ack_connid == 1) && !get_already_done)
			{
				//Impostazione nuovo timer per chiudere la connessione in caso il cient sia inattivo per troppo tempo
				if(close_fd != 0)
				{
					FD_CLR(close_fd, &set);
					if(close(close_fd) == -1)
					{
							perror("close");
							exit(EXIT_FAILURE);
					}
					close_fd = closure_timer(&set);
				}else{
					close_fd = closure_timer(&set);
				}
				printf("Get request received!\n");
				numb_pkt = do_Sget(pktr, sockfd_client, client_info, seq_cmd, ack_cmd, root, &path, &set); 
				if(numb_pkt == -1)
				{
					//Invio di un pacchetto di errore: Il file non è presente sul server
					struct header get_err;
					get_err.ack_connid = ((ack_cmd >> 8) + 2) << 8;
					get_err.seq_tid = (((seq_cmd >> 8) + 1) << 8);
					get_err.flags = 66;
					sprintf(get_err.payload, "%s", "File you require does not exist");
					//INvio pacchetto con probabilità di errore
					srand((unsigned)(time(NULL)));
					double success = (rand() % 10000) / 10000.0;
					if(success > failure_prob)
						send_to(sockfd_client, get_err, client_info);
				}else
				{
					pkt_remaining = numb_pkt;
					get_already_done = 1; //Permette di non 
					memset((void*) &pktr, 0, sizeof(struct header));	
				}

			}
			//Ricezione pacchetto di list
			if((pktr.flags == 5) && (pktr.ack_connid == 1) && !list_already_done)
			{

				//Impostazione nuovo timer per chiudere la connessione in caso il cient sia inattivo per troppo tempo
				if(close_fd != 0)
				{
						FD_CLR(close_fd, &set);
						if(close(close_fd) == -1)
						{
							perror("close");
							exit(EXIT_FAILURE);
						}
						close_fd = closure_timer(&set);
				}else{
					close_fd = closure_timer(&set);	
				}		
				printf("List request received!\n");
				numb_pkt = do_Slist(pktr, sockfd_client, client_info, seq_cmd, ack_cmd, &set);
				pkt_remaining = numb_pkt;
				list_already_done = 1;
				memset((void*) &pktr, 0, sizeof(struct header));	
			}

			//Ricezione FIN
			if((pktr.flags == 32) && (pktr.ack_connid == 1))
			{

				if(close_fd != 0)
				{
						FD_CLR(close_fd, &set);
						if(close(close_fd) == -1)
						{
							perror("close");
							exit(EXIT_FAILURE);
						}
						close_fd = closure_timer(&set);
				}else{
					close_fd = closure_timer(&set);
				}
				fin_ack_seq = ((seq_cmd >> 8) + numb_pkt + 1) << 8;
				struct header fin_ack;
				fin_ack.seq_tid = fin_ack_seq;
				fin_ack.ack_connid = pktr.seq_tid;
				fin_ack.flags = 48;
				close_timer = time(NULL);
				fin_recived = 1;
				srand((unsigned)(time(NULL)));
				double success = (rand() % 10000) / 10000.0;
				if(success > failure_prob)
					send_to(sockfd_client, fin_ack, client_info); //Invio FIN_ACK
				memset((void*) &pktr, 0, sizeof(struct header));
			}

			//Ricezione FIN_ACK
			if(((pktr.flags == 48) && (pktr.ack_connid == ((fin_ack_seq >> 8) +1) << 8)) || ((time(NULL) - close_timer) > 10) && fin_recived == 1)
			{

				if(cmd_pkt[0].time_fd != -1)
				{
					FD_CLR(cmd_pkt[0].time_fd, &set);
					if(close(cmd_pkt[0].time_fd) == -1)
					{
						perror("close fd cmd");
						exit(-1);
					}
				}
				printf("Closing connection");
				//Uccisione del processo
				if(kill(getppid(), SIGUSR1) != 0)
				{
					perror("kill");
					exit(EXIT_FAILURE);
				}
				exit(EXIT_SUCCESS);
			}
			//Ricezione ultimo pacchetto list
			if((pktr.flags == 17) && (pktr.ack_connid == (((seq_cmd >> 8) +1) << 8)))
			{

				if(close_fd != 0)
				{
						FD_CLR(close_fd, &set);
						if(close(close_fd) == -1)
						{
							perror("close");
							exit(EXIT_FAILURE);
						}
						close_fd = closure_timer(&set);
				}
				else{
					close_fd = closure_timer(&set);
				}
				if(cmd_pkt[0].time_fd != -1){
					FD_CLR(cmd_pkt[0].time_fd, &set);
					if(close(cmd_pkt[0].time_fd) == -1){
						perror("close fd cmd");
						exit(-1);
					}
				}
				snd_pkt_in_wndw(sockfd_client, &cmd_buffer, client_info, SLIDING_WND, numb_pkt, &set);  //INvio dati
				cmd_flag = 0;
				seq_cmd = -1;
				memset((void*) &pktr, 0, sizeof(struct header));
			}

			//Riscontro ultimo pacchetto get
			if((pktr.flags == 18) && (pktr.ack_connid == (((seq_cmd >> 8) +1) << 8))) 
			{

				if(close_fd != 0){
						FD_CLR(close_fd, &set);
						if(close(close_fd) == -1){
							perror("close");
							exit(EXIT_FAILURE);
						}
						close_fd = closure_timer(&set);
					}else{
						close_fd = closure_timer(&set);
					}
					
				if(cmd_pkt[0].time_fd != -1){
					FD_CLR(cmd_pkt[0].time_fd, &set);
					if(close(cmd_pkt[0].time_fd) == -1){
						perror("close fd cmd");
						exit(-1);
					}
				}
				snd_pkt_in_wndw(sockfd_client, &cmd_buffer,client_info,SLIDING_WND, numb_pkt, &set); //INvio dati
				cmd_flag = 0;
				//seq_cmd = -1;
				memset((void*) &pktr, 0, sizeof(struct header));
			}

			//Ricezione pacchetto PUT
			if((pktr.flags == 7) && (pktr.ack_connid == 1))
			{ 
				
				if(close_fd != 0)
				{
					FD_CLR(close_fd, &set);
					if(close(close_fd) == -1)
					{
						perror("close");
						exit(EXIT_FAILURE);
					}
					close_fd = closure_timer(&set);
				}else{
					close_fd = closure_timer(&set);
				}

				file_name = malloc(1024 *sizeof(char));
				memset((void *) file_name, 0, sizeof(*file_name));
				get_numpkt(pktr.payload, &numb_pkt, &file_name, &last_pkt_size); //Acquisizione informazioni per payload pkt
				if(fd_opened == 0)
				{
					if((fd_opened = check_if_already_exist(&root, file_name)) == -1){//Se il file esiste invia un messaggio di errore
						pkt = build_pkt(pkt.seq_tid, pkt.ack_connid, 67, "File already exists!");
						fd_opened = 0;
					}
					else
					{

						if(put_already_done == 0)
						{
							printf("Put request received!\n");
							//Costruzione pacchetto di PUT-ACK
							write_already_done = rcv_base = 0;
							pkt.seq_tid = ((seq_cmd >> 8) +1) << 8;
							pkt.ack_connid = pktr.seq_tid;
							pkt.flags = 19;
							//Costruzione buffer di ricezione per il riscontro dei pacchetti
							child_buff = malloc(sizeof(struct pkt_wndw) * numb_pkt);
							unsigned int seq_num = pktr.seq_tid & seq_mask;
							for(int x=0;x<numb_pkt;x++)
							{	

								struct header *packet=malloc(sizeof(struct header));
								memset((void*) packet, 0, sizeof(struct header));
								(child_buff+x)->pkt = packet;
								memset((void*) (child_buff+x)->pkt->payload, 0, sizeof(char *));
								if(x == numb_pkt - 1)
									sprintf((child_buff+x)->pkt->payload, "%d", last_pkt_size); //Last_pkt_size può essere minore di MAXPAYLOAD
								else{
									sprintf((child_buff+x)->pkt->payload, "%d", MAX_PAYLOAD);
								}
								unsigned int seq= ((seq_num >> 8) +x+1) << 8;
								(child_buff+x)->pkt->seq_tid=seq;
							
							}		
							put_already_done = 1;
							memset((void*) &pktr.payload, 0, MAX_PAYLOAD);
						}
						
					}
					
				}
				srand((unsigned)(time(NULL)));
				double success = (rand() % 10000) / 10000.0;
				if(success > failure_prob)
					send_to(sockfd_client, pkt, client_info); //Invio PUT_ACK
				memset((void*) &pktr.payload, 0, MAX_PAYLOAD);
									
			}
		
				

			if((pktr.flags == 18) && (pktr.ack_connid != 1) && (pktr.ack_connid != seq_cmd))
			{

				if(close_fd != 0){
						FD_CLR(close_fd, &set);
						if(close(close_fd) == -1){
							perror("close");
							exit(EXIT_FAILURE);
						}
						close_fd = closure_timer(&set);
					}else{
						close_fd = closure_timer(&set);
					}
					

				ack_sender(cmd_buffer,pktr.ack_connid, &set); //Riscontro pacchetto ricevuto
				time_last_ack = time(NULL);
				snd_pkt_in_wndw(sockfd_client, &cmd_buffer, client_info, SLIDING_WND, numb_pkt, &set);
				for(int x=0;x<numb_pkt;x++)
				{
					if(cmd_buffer[x].ack != 1)
						break;  //Se non sono stati ricevuti tutti i pacchetti esci dal ciclo
					
					if(x == numb_pkt -1)
					{
						struct header pkt = build_pkt(((pktr.ack_connid >> 8) +1) << 8, pktr.seq_tid, 10, "Get done successfully!");
						cmd_pkt[0].pkt = malloc(sizeof(struct header));
						*(cmd_pkt[0].pkt) = pkt;
						cmd_pkt[0].ack = 0;
						cmd_pkt[0].time_fd = -1;
						cmd_pkt[0].jumped = 0;
						next_seqnum = snd_base = 0;
						cmd_flag = 1;
						snd_pkt_in_wndw(sockfd_client, &cmd_pkt, client_info, 1, 1, &set); //Invio pacchetto di risposta alla PUT
					}
					
				}
				memset((void*) &pktr, 0, sizeof(struct header));
			}

			if(pktr.flags == 26 || pktr.flags == 25)
			{

				if(close_fd != 0){
						FD_CLR(close_fd, &set);
						if(close(close_fd) == -1){
							perror("close");
							exit(EXIT_FAILURE);
						}
						close_fd = closure_timer(&set);
					}else{
						close_fd = closure_timer(&set);
					}
					

				if(cmd_pkt[0].time_fd != -1)
				{
					FD_CLR(cmd_pkt[0].time_fd,&set);
					if(close(cmd_pkt[0].time_fd) == -1){
						perror("close fd cmd");
						exit(-1);
					}
				}
				printf("Deallocation of resources get/list\n");
				cmd_flag = -1;
				next_seqnum = snd_base = get_already_done = 0;
				free(cmd_buffer);
				free(cmd_pkt);
				FD_ZERO(&set);
				FD_ZERO(&return_set);
				FD_SET(sockfd_client, &set);
				memset((void*) &pktr, 0, sizeof(struct header));
			}

			//Riscontro pacchetti list
			if((pktr.flags == 1) && (pktr.ack_connid != 1/*0*/) && (pktr.ack_connid != seq_cmd))
			{


				if(close_fd != 0)
				{
					FD_CLR(close_fd, &set);
					if(close(close_fd) == -1)
					{
							perror("close");
							exit(EXIT_FAILURE);
					}
					close_fd = closure_timer(&set);
				}else{
					close_fd = closure_timer(&set);
				}
				ack_sender(cmd_buffer,pktr.ack_connid, &set); //Riscontro pacchetti arrivati
				time_last_ack = time(NULL);
				snd_pkt_in_wndw(sockfd_client, &cmd_buffer, client_info, SLIDING_WND, numb_pkt, &set); //Invio pacchetti dopo un'eventuale scorrimento della finestra
				for(int x=0;x<numb_pkt;x++)
				{
					if(cmd_buffer[x].ack != 1)
						break; //Se non sono stati ricevuti tutti i pacchetti esci dal for
					
					if(x == numb_pkt -1)
					{
						struct header pkt = build_pkt(((pktr.ack_connid >> 8) +1) << 8, pktr.seq_tid, 9, "List done successfully!");
						list_already_done = 0;
						cmd_pkt[0].pkt = malloc(sizeof(struct header));
						*(cmd_pkt[0].pkt) = pkt;
						cmd_pkt[0].ack = 0;
						cmd_pkt[0].time_fd = -1;
						cmd_pkt[0].jumped = 0;
						next_seqnum = snd_base = 0;
						cmd_flag = 1;
						snd_pkt_in_wndw(sockfd_client, &cmd_pkt, client_info, 1, 1, &set); //Invio pacchetto di risposta list
					}
					
				}
				memset((void*) &pktr, 0, sizeof(struct header));
			}

			//Riscontro pacchetti put
			if((pktr.flags == 3) && (pktr.ack_connid != 1))
			{		
				if(close_fd != 0)
				{
						FD_CLR(close_fd, &set);
						if(close(close_fd) == -1)
						{

							perror("close");
							exit(EXIT_FAILURE);
						}
						close_fd = closure_timer(&set);
				}else{
					close_fd = closure_timer(&set);
				}
					
				if(put_already_done != 0)
					ack_reciver(child_buff,content,pktr.seq_tid,pktr.payload,numb_pkt); //Gestione finestra di ricezione in base all'arrivo dei pacchetti
				struct header pkt;
				if(rcv_base == numb_pkt) //Tutti i pacchetti sono stati ricevuti
					pkt = build_pkt(((pktr.ack_connid >> 8) + 1) << 8, pktr.seq_tid, 27, "Put done successfully"); //Invio messaggio put di successo
				else{
					pkt = build_pkt(((pktr.ack_connid >> 8) + 1) << 8, pktr.seq_tid, 19, "\0"); //Invio pacchetto di risposta
				}
				for(int x=0;x<numb_pkt;x++)
				{	
					if(!child_buff[x].ack)
						break; //Non ho ricevuto tutti i pacchetti
					if(x == numb_pkt-1 && !write_already_done) 
					{
						//Ricerca sull'albero della cartella dove salvare il file
						path = malloc(1024 * sizeof(char));
						memset((void *) path, 0,sizeof(*path));
						sprintf(path, "%s", "doc/");
						search(&root, file_name, path);
						strcat(path, file_name);
						write_on_filelist(file_name); //Scrittura sul filelist
						for(int i=0;i<numb_pkt;i++)
						{	
							if(i == numb_pkt-1)
								write_to_fd(fd_opened, child_buff[i].pkt->payload, last_pkt_size); //Scrittura dell'ultimo pkt
							else{
								write_to_fd(fd_opened, child_buff[i].pkt->payload, 1483);
							}
						}
						write_already_done = 1;
						if(close(fd_opened) == -1){
							perror("close");
							exit(-1);
						}
						free(child_buff);
						put_already_done = fd_opened = 0;
						printf("Deallocating resources put\n");
					}
				}
				srand((unsigned)(time(NULL)));
				double success = (rand() % 10000) / 10000.0;
				if(success > failure_prob)
					send_to(sockfd_client, pkt, client_info); //Invio ack
				memset((void *)&pktr, 0,sizeof(pktr));
			}
		}
	}
}
	
void removeIP_handler(int sig, siginfo_t *si, void *context)
{

	pid_t pid = si->si_pid;
	remove_client(&client_list, pid);
	struct node_t *p;
}

void removeIP(void)
{

	struct sigaction sa;
	sa.sa_sigaction = &removeIP_handler;
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGUSR1, &sa, NULL) < 0) 
	{
		perror ("sigaction");
		exit(EXIT_FAILURE);
	}

}


int main(int argc, char ** argv)
{

	//Definizione variabili
	int maxi, maxd;
	fd_set read_fds ,master;
	int ready,client[FD_SETSIZE];
	int n;
	int len3;
	int sockfd,sockfd_client,len;
	struct sockaddr_in addr;
	pid_t pid;
	sigset_t mask, orig_mask;
	struct timeval tm; 
	tm.tv_sec = 0;
	tm.tv_usec = 0;
	//Inizializzazioni strutture server e socket di benvenuto
	memset((void *)&addr,0,sizeof(addr));
	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=htonl(INADDR_ANY);
	addr.sin_port=htons(SERV_PORT);
	sockfd = get_sockfd();// socket di benvenuto
	struct sockaddr_in s;
	s.sin_addr.s_addr = 0;
	do_bind(sockfd, &addr);
	struct header con_pkt_rcv;//Pacchetto di richiesta connessione ricevuto dal padre
	struct header con_pkt_snd;
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_SET(sockfd, &master);//inserimento welcome socket nella select
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	
	for(;;)
	{

 		read_fds = do_select_block(master, sockfd); 

		removeIP();
		
		if(FD_ISSET(sockfd, &read_fds))
			start_connection(&con_pkt_snd, &con_pkt_snd, sockfd, pid, &addr); //COntrollo se il pacchetto arrivato è un SYN e in tal caso stabilisco la connessione

	}

}




