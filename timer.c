#include "basic.h"

#define timer_fisso 0

/*Crea un file descriptor al quale è collegato un evento di timeout*/
void set_timer(struct pkt_wndw *pkt, fd_set *fd_timer){

	struct itimerspec tm; /*Struttura utilizzata per impostare dopo quanto deve verificarsi il timeout*/

	if(timeout > 900000000) /*Limite superiore..*/
		timeout = 700000000;
	if(timeout < 100000000)/*..ed inferiore per il timer adattativo*/
		timeout = 100000000;

	tm.it_value.tv_sec = (time_t) timer_fisso; /*Specifica i secondi passati i quali il timer scade*/
	tm.it_value.tv_nsec = (long) timeout; /*Se si vuole specificare il tempo in nanosecondi*/
	/*Servono per implementare un timer che scada più di una volta*/
	tm.it_interval.tv_sec = (time_t) 0;
	tm.it_interval.tv_nsec = (time_t) 0;

	/*Se a quel pacchetto non è mai stato associato un timer se ne crea uno da zero..*/
	if(pkt->time_fd == -1){
		int fd = timerfd_create(CLOCK_REALTIME, 0); /*Creo il file descriptor*/
		if(fd == -1){
			perror("timerfd_create");
			exit(-1);
		}
		if (timerfd_settime(fd, 0, &tm, NULL) == -1){ /*Associo al file descriptor un timer che scade dopo un tempo specificato dalla struttura tm*/
			perror("settime");
			exit(-1);
		}
		pkt->time_fd = fd;
		FD_SET(pkt->time_fd, fd_timer); /*Aggiungo il file descriptor del timer appena creato all'fd_set affichè possa essere controllato dalla select()*/
	}else{
		/*..altrimenti, per evitare un massivo utilizzo di file descripotor, gli si associa l'fd precedente*/
		if (timerfd_settime(pkt->time_fd, 0, &tm, NULL) == -1){
			perror("settime");
			exit(-1);
		}
	}
}

/*Crea un timer utilizzato nel momento in cui il client(o il server) non riceve più nulla dall'altro lato*/
int closure_timer(fd_set *fdset){

	struct itimerspec tm; /*Struttura utilizzata per impostare dopo quanto deve verificarsi il timeout*/

	/*Imposta la scadenza del timer dopo 180 secondi*/
	tm.it_value.tv_sec = (time_t) 180;
	tm.it_value.tv_nsec = (long) 0;
	tm.it_interval.tv_sec = (time_t) 0;
	tm.it_interval.tv_nsec = (time_t) 0;

	int fd = timerfd_create(CLOCK_REALTIME, 0); /*Creo il file descriptor*/
	if(fd == -1){
		perror("timerfd_create");
		exit(-1);
	}
	if (timerfd_settime(fd, 0, &tm, NULL) == -1){ /*Associo al file descriptor un timer che scade dopo un tempo specificato dalla struttura tm*/
		perror("settime");
		exit(-1);
	}
	FD_SET(fd, fdset);  /*Aggiungo il file descriptor del timer appena creato all'fd_set affichè possa essere controllato dalla select()*/
	return fd;
}


