#include "list.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

/*Funzione che alloca un nuovo nodo all'interno della lista collegata*/
struct node_t *alloc_node(void)
{
	struct node_t *p;

	p = malloc(sizeof(struct node_t));

	if (p == NULL) {
		fprintf(stderr, "alloc_node: failed.\n");
		exit(-1);
	}

	return p;
}

/*Inserisco un elemento della lista che corrisponde ad un uovo client*/
void insert_new_client(unsigned long client_IP, pid_t pid, struct node_t **phead , unsigned short port)
{
	struct node_t *new;

	new = alloc_node(); /*Creo un nuovo elemento..*/
	new->port= port;/*..inserisco la porta del client..*/
	new->IP_address = client_IP; /*..il suo indirizzo IP..*/
	new->child_pid = pid; /*..ed il pid del processo del figlio che lo gestisce*/
	insert_after_node(new, phead); /*Infine lo inserisco all'interno della lista*/
}

/*Rimuovo il nodo p*/
void free_node(struct node_t *p)
{
	free(p);
}

/*Elimino l'intera lista*/
void free_all_nodes(struct node_t **h)
{
	struct node_t *p;

	while (*h != NULL) {
		p = remove_after_node(h);
		free_node(p);
	}
}

/*Inserisco un nuovo spostando di conseguenza i successivi*/
void insert_after_node(struct node_t *new, struct node_t **pnext)
{
	new->next = *pnext;
	*pnext = new;
}

/*Rimuovo un nodo spostando di conseguenza i successivi*/
struct node_t *remove_after_node(struct node_t **ppos)
{
	struct node_t *r = *ppos;
	*ppos = r->next;
	return r;
}

/*Rimuovo il client gestito dal processo specificato da pid*/
void remove_client(struct node_t **ppos, pid_t pid)
{
	struct node_t *r = *ppos;
	if(r->child_pid == pid){
		remove_after_node(ppos);
	}
}

int check_is_in(struct node_t **pnext, unsigned char cd){

struct node_t *p;

for (p = *pnext; p != NULL; p = p->next) {
		if(p->value == cd){
			return 1;// già presente in lista
		}
	}
	return 0;//non presente
}

/*Controllo se un client è già presente nella lista*/
int check_is_connected(struct node_t **pnext, unsigned long IP_addr, unsigned short port){

struct node_t *p;

for (p = *pnext; p != NULL; p = p->next) {
		if((p->IP_address == IP_addr && p->port == port)){
			return 1;// già presente in lista
		}
	}
	return 0;//non presente
}

