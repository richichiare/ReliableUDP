CC = gcc

LIBS = -lm  

CEXE = client_udp
SEXE = server_udp

.PHONY : clean

clean :
	rm -f $(SEXE) $(CEXE)  
server :
	$(CC) list.c basic.c tree.c timer.c server.c connection.c -o $(SEXE) -lm
client:
	$(CC) list.c basic.c tree.c command.c timer.c client.c -o $(CEXE) -lm
all:
	server client
