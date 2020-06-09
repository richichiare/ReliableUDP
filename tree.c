#include "tree.h"
#include "basic.h"

/*Funzione che inserisce un elemento all'interno dell'albero binario*/
void insert(node **tree, char *dir) {

	node *temp = NULL;

	if(!(*tree)) {

		temp = (node *) malloc(sizeof(node));

		temp->left = temp->right = NULL;
		temp->dir = dir;
		*tree = temp;
		return;
	}

	int c = strcmp(dir, (*tree)->dir);

	if(c < 0) { /*Se c<0 allora il nuovo nodo si trova prima..*/
		insert(&(*tree)->left, dir);

	} else if(c > 0) { /*..altrimenti va dopo*/
		insert(&(*tree)->right, dir);
	}
}

/*Funzione che cerca restituisce il path all'interno del quale può essere salvato il file*/
int search(node **tree, char *file, char *path){

	if(!(*tree)) {
		return -1;
	}

	int lowfile = tolower(file[0]);

	if(lowfile < (*tree)->dir[0]){
		strcat(path, (*tree)->dir);
		strcat(path, "/");
		search(&((*tree)->left), file, path);
	} else if(lowfile > (*tree)->dir[2]){
		strcat(path, (*tree)->dir);
		strcat(path, "/");
		search(&((*tree)->right), file, path);
	} else{
		for (int i = 0; i < 3; i++) {

			if(lowfile == (*tree)->dir[i]){
			strcat(path, (*tree)->dir);
			strcat(path, "/");
			return 0;;
			}
		}
	}

	return 0;
}

/*Routine che controlla se il file passato come argomento esiste o meno*/
int check_if_already_exist(node **tree, char *file){

	char *test_path = malloc(1024 * sizeof(char));
	int fd;

	sprintf(test_path, "%s", "doc/");
	search(tree, file, test_path); /*Ottengo il path del file*/
	strcat(test_path, file);

	errno = 0;
	if((fd = open(test_path, O_EXCL | O_CREAT | O_RDWR, 0777)) == -1){
		/*Se errno viene impostato ad EEXIST allora il file già esiste*/
		if(errno !=0 && errno == EEXIST){
			free(test_path);
			return -1;
		}
	}else{
		/*Altrimenti ritorno il file descriptor associato*/
		if(mylock(fd, F_WRLCK) == -1){
			perror("mylock in put");
			exit(EXIT_FAILURE);
		}
		free(test_path);
		return fd;
	}
}