struct binary_tree{
	
	char *dir;
	struct binary_tree *left, *right;
};


typedef struct binary_tree node;

node *root;

void insert(node **, char *);
int search(node **, char *, char *);
int check_if_already_exist(node **, char *);
