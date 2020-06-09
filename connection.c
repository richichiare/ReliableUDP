#include "basic.h"
#include "list.h"

struct pkt_wndw *cmd_pkt_client;
int sockfd_client;



/*Funzione che permette al server di controllare se il pacchetto con_pkt_rcv è un pacchetto di richiesta di connessione*/
struct header check_isConnection(struct header con_pkt_rcv){

	struct header con_pkt_snd;

	if(con_pkt_rcv.flags == 128){/*Il flag 128 corrisponde al SYN*/
			
		con_pkt_snd.ack_connid = (con_pkt_rcv.seq_tid); 
		
		/*Genero il numero di sequenza casuale*/
		srand((unsigned ) time(NULL) + 1);
		con_pkt_snd.seq_tid = (rand() % (max_seq - min_seq)+ min_seq) << 8; 

		con_pkt_snd.flags = 128 + 16; /*Se si tratta di una richiesta di SYN allora imposto il flag di SYN-ACK*/
	}
	else{
		con_pkt_snd.flags = 64; /*Altrimenti il flag di RST*/
	}
	return con_pkt_snd;
}

/*Funzione che si occupa di inizializzare la connessione con il client*/
void start_connection( struct header * pkts, struct header * pktr , int sockfd, pid_t pid, struct sockaddr_in * address ){

	struct header con_pkt_snd = *pkts;
	struct header con_pkt_rcv = *pktr; 
	struct sockaddr_in addr = *address;


	read_from_skt(&addr, sockfd, &con_pkt_rcv);

	/*Per evitare che uno stesso client instauri più connessioni a causa del continuo rinvio del pacchetto di SYN*/
	if(!check_is_connected(&client_list, addr.sin_addr.s_addr, addr.sin_port)){

		con_pkt_snd = check_isConnection(con_pkt_rcv);/*Ritorna il pacchetto che dovrà inviare il figlio*/

		/*Se effetivamente era una richiesta di connessione*/
		if(con_pkt_snd.flags == 144){

			/*Crea un processo figlio..*/
			if((pid = fork()) == -1){
					perror("In fork()");
					exit(-1);
			}

			/*Il processo figlio chiude la socket del processo padre*/
			if(pid == 0){
				if(close(sockfd) == -1){
					perror("Closing wlcome socket");
					exit(-1);
				}
				child_job(con_pkt_snd, addr); /*..al quale farà gestire il client*/
			}
			/*Inserisce il client all'interno di una lista che tiene conto dei client che il server sta gestendo*/
			insert_new_client(addr.sin_addr.s_addr, pid, &client_list, addr.sin_port);
			struct node_t *p;	
		}

	}
	else{
		srand((unsigned)(time(NULL)));
		double success = (rand() % 10000) / 10000.0;
		//if(success > failure_prob){
		//	send_to(sockfd, con_pkt_snd, addr); //Sending RST pkt as a connection refused
		//}
	}
		

}

/*void close_connection(unsigned int seq, struct sockaddr_in addr, fd_set *fd_timer){

	struct header pkt = build_pkt(seq, 1, 32, "\0");//Special put pkt

	cmd_pkt_client = malloc(sizeof(struct pkt_wndw));

	cmd_pkt_client[0].pkt = malloc(sizeof(struct header));
	*(cmd_pkt_client[0].pkt) = pkt;
	cmd_pkt_client[0].ack = 0;
	cmd_pkt_client[0].time_fd = -1;
	cmd_pkt_client[0].jumped = 0;

	snd_pkt_in_wndw(sockfd_client, &cmd_pkt_client, addr, 1, 1, fd_timer);
	next_seqnum = 0;
	cmd_flag = 1;
}
*/