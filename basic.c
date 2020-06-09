#include "basic.h"
#include "timer.h"

/*Crea una socket e restituisce il file descriptor associato*/
int get_sockfd(void){
	int sockfd;

	if((sockfd=socket(AF_INET,SOCK_DGRAM,0))<0) /*Creo una socket che utilizza la cui famiglia di indirizzi è IPv4(AF_INET) 
														e che supporta lo scambio di datagrammi(non orientato alla connessione e non affidabile)*/
	{
		perror("errore in socket");
		exit(-1);
	}

	return sockfd;
}

/*Effettua l'invio del pacchetto*/
void send_to(int sockfd, struct header snd_pkt, struct sockaddr_in servaddr){

	/*Invia il pacchetto specificato snd_pkt attraverso la socket il cui file descriptor è specificato da sockfd 
		al server il cui indirizzo IP e porta sono specificati nella struttura servaddr*/
	if(sendto(sockfd,(struct header *) &snd_pkt, sizeof(snd_pkt), 0, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0){
		perror("Error in sendto");
		exit(-1);
	}
}


void do_bind(int sockfd, struct sockaddr_in *addr){

	memset((void *) addr, 0 , sizeof(addr));
	addr->sin_family=AF_INET;/*Famiglia di indirizzi IPv4*/
	addr->sin_addr.s_addr=htonl(INADDR_ANY);/*La socket verrà associata qualsiasi indirizzo*/
	addr->sin_port=htons(SERV_PORT);/*Porta a cui è associata la socket*/

	/*Associo la struttra addr di tipo sockaddr alla socket creata precedentemente da socket()*/
	if(bind(sockfd,(struct sockaddr *)addr,sizeof(*addr))<0){
		perror("errore in bind");
		exit(-1);
	}
}

fd_set do_select(fd_set master, int maxd, struct timeval *tm){

	int ready;
	fd_set read_fds;//Descrittori di lettura
	read_fds = master;

	/*La select() restituisce il numero di descrittori trovati attivi tra quelli presenti in read_fds*/
	if((ready = select(maxd, &read_fds, NULL , NULL , tm))<0){
		perror("in select");
		exit(-1);
	}
	return read_fds;
}

/*A differenza della precedente funzione questa select è bloccante*/
fd_set do_select_block(fd_set master, int maxd){

	int ready;
	fd_set read_fds;//Descrittori di lettura
	read_fds = master;

	errno = 0;

	if((ready = select(maxd + 1, &read_fds, NULL , NULL , NULL))<0){
		/*Nel caso in cui la select() ritornasse ed errno è uguale ad EINTR allora è stata interrotta da un segnale*/
		if(errno == EINTR){ 
			return read_fds;
		}else{
			perror("in select");
			exit(-1);
		}
		
	}
	return read_fds;
	
}

/*Funzione che implementa la lettura dalla socket il cui descrittore è sockfd*/
void read_from_skt(struct sockaddr_in *client_addr, int sockfd, struct header *con_pkt_rcv){

	int n;
	socklen_t len =sizeof(*client_addr);
	memset((void *)client_addr,0,sizeof(*client_addr));

	memset((void *)con_pkt_rcv->payload,0,sizeof(*con_pkt_rcv->payload));

	errno = 0;
	/*Legge dalla socket un pacchetto e lo inserisce in con_pkt_rcv*/
	n = recvfrom(sockfd, (struct header *) con_pkt_rcv , sizeof(*con_pkt_rcv) ,0,(struct sockaddr *) client_addr, &len);
	/*Nel caso in cui la recvfrom() ritornasse ed errno è uguale ad EINTR allora è stata interrotta da un segnale*/
	if((n < 0) && (errno != EINTR)){
		perror("errore in recvfrom");
		exit(-1);
	}
	if(n>0){
		return;
	}
}

/*Funzione che implementa la scrittura su file del contenuto di buf*/
void write_to_fd(int fd, unsigned char *buf, ssize_t size){

	ssize_t v;

	while (size > 0) {
		v = write(fd, buf, size);
		if (v == -1) {
			perror("Error while writing file\n");
			exit(-1);
		}
		size -= v;
		buf += v;
	}
}

int mylock(int fd, int type)
{
	struct flock fl;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = type;
	return fcntl(fd, F_SETLKW, &fl);
}

/*Funzione che implementa la scrittura sul filelist del nomeFile passato come parametro*/
void write_on_filelist(char *nomeFile){

	int fd; 
	if((fd = open("doc/filelist.txt", O_RDWR, 0777)) == -1){
		perror("Open filelist");
		exit(-1);
	}

	if(mylock(fd, F_RDLCK | F_WRLCK) == -1){
		perror("mylock in filelist.txt");
		exit(EXIT_FAILURE); 
	}

	lseek(fd, 0, SEEK_END); /*Sposta il puntatore fino al punto in cui è avvenuta l'ultima scrittura*/

	write_to_fd(fd, nomeFile, strlen(nomeFile)); /*Scrivo il nome del file*/

	write_to_fd(fd, "\n", strlen("\n"));

	lseek(fd, 0, SEEK_SET); /*Infine riporto il puntatore alla posizione originaria*/

	if(close(fd) == -1){
		perror("Close filelist.txt");
		exit(-1);
	}
}

/*Calcola la dimensione del file per poi implementare correttamente la frammentazione*/
size_t get_file_length(int fd){
	size_t v = lseek(fd , 0, SEEK_END);
	if(v==( off_t) -1)
	{
		fprintf(stderr,"Errore in lseek");
		exit(-1);
	}
	return  v;
}

/*Apertura del file il cui path è specificato da name*/
int  open_file(const char *name)
{
	int fd = open(name , O_RDONLY);  
	if(fd==-1)
	{
		printf("%s\n", "Not valid input file, try again");
		return -1;
	}
	return  fd;
}

/*Funzione il cui compito è quello di fare il parsing della stringa s per ottenere alcune informazioni necessarie al comando PUT*/
void *get_numpkt(char *s, int *numpkt, char **file_name, int *last_pkt_size){

	char *msg, *p;
	int i = 0;

	errno = 0;
	*numpkt = strtoul (s , &msg , 10); /*La strtoul() ritorna il numero trovato nella stringa s posizionando in msg il resto della stringa*/
	if (errno != 0){
		fprintf ( stderr ,"Invalid number\n") ;
		return NULL;
	}

	/*Continuo a fare il parsing della stringa finche non terminano gli spazi bianchi*/
	while(!(isblank(msg[i]))){
		(*file_name)[i] = msg[i];
		i++;
	}

	/*Ho ottenuto il nome del file che sto ricevendo*/
	(*file_name)[i] = '\0';

	errno = 0;
	*last_pkt_size = strtoul(msg+ i + 1, &p, 10);/*Infine la dimensione dell'ultimo pacchetto da ricevere*/
	if (errno != 0){
		fprintf ( stderr ,"Invalid number\n") ;
		return NULL;
	}
}

/*Funzione che implementa l'invio di un pacchetto  se e solo se si trova all'interno della finestra di spedizione*/
fd_set snd_pkt_in_wndw(int sockfd, struct pkt_wndw **pkt_buf, struct sockaddr_in servaddr, int sliding_wndw, int numpkt, fd_set *fd_timer){

	int fd;	

	struct pkt_wndw *x = *pkt_buf;
	struct timespec start;

	/*Solo se il pacchetto cade all'interno della finestra allora posso inviarlo(o comunque simularne l'invio)*/
	if(next_seqnum < (snd_base + sliding_wndw)){

		/*Fintanto che ci sono pacchetti nella finestra che possono essere inviati*/
		for(int i=next_seqnum; ((i < ((snd_base + sliding_wndw))) && (i<numpkt));i++){ 
			/*Genero un numero casuale tra 0 e 1 usando come seme il numero di secondi corrente dal 1970*/
			srand((unsigned)(time(NULL)));
			double success = (rand() % 10000) / 10000.0;
			if(success > failure_prob){ /*Se success è minore di failure_prob simulo la perdita del pacchetto*/

				send_to(sockfd, *(x[i]).pkt, servaddr); /*Invio il pacchetto tramite la socket al server*/

				if (x[i].jumped != 1)
					next_seqnum++;

				if(x[i].time_fd == -1){
					clock_gettime(CLOCK_REALTIME, &start);
					x[i].dyn_timer = start.tv_nsec;
				} 

				x[i].ack=2; /*ack=2 pacchetto inviato ma non riscontrato*/
				set_timer(&x[i], fd_timer); /*Imposto il timer di ritrasmissione*/
			
			}else{
				x[i].jumped = 1;
				next_seqnum++; /*Incremento il numero di sequenza*/
				set_timer(&x[i], fd_timer); /*Imposto il timer di ritrasmissione*/
			}
		}
	}
}

/*Funzione che implementa il ri-invio di un pacchetto dopo che si è verificato il timeout dello stesso*/
void re_send(int sockfd, struct pkt_wndw **pkt_buf, int pos, struct sockaddr_in servaddr, int sliding_wndw,int numpkt, fd_set *fd_timer){

	struct pkt_wndw *x = *pkt_buf;

	if(x[pos].ack == 2 || (x[pos]).jumped == 1){/*Se il pacchetto è stato già inviato ma non riscontrato oppure il cui invio è saltato*/
		srand((unsigned)(time(NULL)));
		double success = (rand() % 10000) / 10000.0;
		if(success > failure_prob){
			send_to(sockfd, *(x[pos]).pkt, servaddr);
		}
		set_timer(&x[pos], fd_timer); /*Imposto il timer di ritrasmissione*/
	}else{
		snd_pkt_in_wndw(sockfd, pkt_buf, servaddr, sliding_wndw, numpkt, fd_timer);
	}
}

/*Funzione che implementa lo scorrimento della finestra di spedizione*/
void slide_wndw(struct pkt_wndw *buf){

	int i = 0;
	struct pkt_wndw *start = (buf+snd_base);
	while((start+i)->ack == 1){
		snd_base++;
		i++;
	}
}

/*Funzione che implementa lo scorrimento della finestra di ricezione*/
void slide_wndw_rcv(struct pkt_wndw *buf){
	int i = 0;
	
	struct pkt_wndw *start = (buf+rcv_base);
	while((start+i)->ack == 1){

		rcv_base++;
		i++;
	}
	
}

/*Funzione usata per la costruzione di un pacchetto di acknowledgment*/
struct header build_pkt(unsigned int seq_tid, unsigned int ack_connid, unsigned char flags, char *payload){

	struct header new_pkt;

	new_pkt.seq_tid = seq_tid;
	new_pkt.ack_connid = ack_connid;
	new_pkt.flags = flags;
	strcpy(new_pkt.payload, payload);

	return new_pkt;
}

void read_from_pipe(int pipe_fd[2], unsigned char *buf, ssize_t size){

	ssize_t v;
	while (size > 0) {
		v = read(pipe_fd[0], buf, size);
		if (v == -1) {
			perror("Error while reading file\n");
			exit(-1);
		}
		size -= v;
		buf += v;
	}
}

/*Funzione usata per la costruzione di un pacchetto*/
struct header build_pkt_file(unsigned int seq_tid, unsigned int ack_connid, unsigned char flags, unsigned char *payload, int payload_size){

	struct header new_pkt;

	new_pkt.seq_tid = seq_tid;
	new_pkt.ack_connid = ack_connid;
	new_pkt.flags = flags;

	/*Viene effettuata la copia byte a byte all'interno del pacchetto*/
	for (int i = 0; i < payload_size; ++i)
	{
		new_pkt.payload[i] = payload[i];
	}

	return new_pkt;
}

/*Funzione che data una stringa ottiene il suo contenuto ossia il numero di pacchetti stimati nel comando GET*/
void *get_pkt_size(char *s, int *numpkt){

	char *msg;
	errno = 0;
	*numpkt = strtoul (s , &msg , 10);

	if (errno != 0){
		fprintf ( stderr, "Invalid number \n") ;
		return NULL;
	}
}


/*Funzione utilizzata lato ricevente che oltre ad immagazinare i dati ricevuti dai pacchetti sposta la finestra di ricezione*/
void ack_reciver(struct pkt_wndw *buf,char *data,unsigned int seq_num, unsigned char *text,int numpkt){

	unsigned int seq_num_pkt;
	struct header *x;
	int dim;
	struct pkt_wndw *check_pkt;
	unsigned int seqnum_mask = 0xFFFFFF00;
	seq_num = (seq_num & seqnum_mask) >> 8;/*Ottengo l'ack a 24 bits*/

	/*Scorro la finestra di ricezione per controllare quale pacchetto ho ricevuto*/
	for(int i=rcv_base; (i < rcv_base+SLIDING_WND) && (i<numpkt); i++){

		check_pkt = (buf+i);
		x = (check_pkt->pkt);
		seq_num_pkt = ((x->seq_tid) & seqnum_mask) >> 8;

		/*Se il numero di sequenza ricevuto corrisponde ad uno di quelli dei pacchetti presenti all'interno della finestra*/
		if(seq_num_pkt == seq_num){

			if((buf+i)->ack != 1){/*Se si tratta di un pacchetto che non ho già riscontrato..*/

				get_pkt_size(buf[i].pkt->payload, &dim);/*..ottengo la sua dimensione..*/
				memset((void *) &(buf[i].pkt->payload), 0, sizeof(buf[i].pkt->payload));

				/*..ne effettuo la copia byte a byte*/
				for(int j = 0; j < dim; j++){
					(buf[i].pkt->payload)[j] = text[j];
				}

				(buf+i)->ack = 1;/*Lo segno come pacchetto riscontrato*/
			}
			
			/*Se il pacchetto ricevuto corrisponde alla base della finestra di ricezione allora sposto tale finestra*/
			if(i == rcv_base){
				slide_wndw_rcv(buf);
			}

			break;
		}
	}
}

/*Funzione utilizzata lato mittente per ricevere il riscontro dei pacchetti inviati all'interno della finestra*/
void ack_sender(struct pkt_wndw *buf,unsigned int ack_num, fd_set *fd_timer){

	unsigned int seq_num;
	struct header *x;
	struct pkt_wndw *check_pkt;
	unsigned int ACK_seqnum_mask = 0xFFFFFF00;//Ack's mask & Seq_num's mask
	ack_num = (ack_num & ACK_seqnum_mask) >> 8;//Ho l'ack a 24 bits
	struct timespec now;
	
	for(int i=snd_base; i < next_seqnum; i++){
		check_pkt = (buf+i);
		x = (check_pkt->pkt);
		seq_num = ((x->seq_tid) & ACK_seqnum_mask) >> 8;
		
		/*Se il numero di sequenza ricevuto corrisponde ad uno di quelli dei pacchetti presenti all'interno della finestra*/
		if(seq_num == ack_num){

			if(is_dyn == 1){ /*Se l'opzione del timer dinamico attiva ne effettuo il calcolo*/
				clock_gettime(CLOCK_REALTIME, &now);
				long sample_rtt = now.tv_nsec - check_pkt->dyn_timer;
				ext_rtt = (1 - 0.125) * ext_rtt + 0.125 * sample_rtt;
				dev_rtt = (1 - 0.25) * dev_rtt + 0.25 * abs(sample_rtt - ext_rtt);
				timeout = (ext_rtt + 4 * dev_rtt);
			}

			/*Se ho riscontrato un pacchetto il cui timer di ritrasmissione era stato settato*/
			if(check_pkt->time_fd != -1){
				FD_CLR(check_pkt->time_fd, fd_timer);/*Elimino l'fd dal set controllato dalla select*/
				if(close(check_pkt->time_fd) == -1){ /*E libero risorse per il SO*/
					perror("close fd");
					exit(-1);
				}
			}	
			check_pkt->time_fd = -1;
			(buf+i)->ack = 1; /*Segno il pacchetto come riscontrato*/
			/*Se il pacchetto riscontrato corrisponde con quello alla base della finestra allora traslo la finestra in avanti*/
			if(i == snd_base){
				slide_wndw(buf);
			}
			break;
		}
	}
}


