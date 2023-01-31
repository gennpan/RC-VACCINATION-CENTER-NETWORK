#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>      // libreria standard del C per la gestione delle situazioni di errore
#include <string.h>
#include <netdb.h>      // contiene le definizioni per le operazioni del database di rete
#include <sys/types.h>
#include <sys/socket.h> //contiene le definizioni dei socket
#include <arpa/inet.h>  // contiene le definizioni per le operazioni Internet
#define BUFF_MAX_SIZE 1024   //dimensione massima del buffer size
#define COD_SIZE 17      //dimensione del codice di tessera sanitaria 16 byte ed 1 del terminatore
#define ACK_SIZE 64     //dimensione del messaggio di ACK ricevuto dal Centro Vaccinale

//Struct del pacchetto che l'Utente deve inviare al Centro Vaccinale
typedef struct {
    char nome[BUFF_MAX_SIZE];
    char cognome[BUFF_MAX_SIZE];
    char cod_fisc[COD_SIZE];
} VACCINAZIONE;

//Legge esattamente count byte s iterando opportunamente le letture
ssize_t full_read(int fd, void *buffer, size_t count) {
    size_t n_left;
    ssize_t n_read;
    n_left = count;
    while (n_left > 0) {  // ciclo ripetuto fintanto che non vengono letti tutti i bytes
        if ((n_read = read(fd, buffer, n_left)) < 0) {
            if (errno == EINTR) continue; 
            else exit(n_read);
        } else if (n_read == 0) break; // se sono 0 si chiude il descrittore,si esce dal ciclo e viene restituito n_left
        n_left -= n_read;
        buffer += n_read;
    }
    buffer = 0;
    return n_left;
}


//Scrive esattamente count byte s iterando opportunamente le scritture
ssize_t full_write(int fd, const void *buffer, size_t count) {
    size_t n_left;
    ssize_t n_written;
    n_left = count;
    while (n_left > 0) {          // ciclo ripetuto fintanto che non vengono scritti tutti i bytes
        if ((n_written = write(fd, buffer, n_left)) < 0) {
            if (errno == EINTR) continue; 
            else exit(n_written); //Se non è una System Call, esci con un errore
        }
        n_left -= n_written;
        buffer += n_written;
    }
    buffer = 0;
    return n_left;
}

//Funzione per la creazione del pacchetto da inviare al CentroVaccinale
VACCINAZIONE crea_pacchetto() {
    char buffer[BUFF_MAX_SIZE];
    VACCINAZIONE crea_pack;

    //Inserimento del nome
    printf("Inserisci il nome: ");
    if (fgets(crea_pack.nome, BUFF_MAX_SIZE, stdin) == NULL) {
        perror("fgets() error");
    }
    //inserimento del terminatore al posto dell'invio inserito dalla fgets, poichè questo veniva contato ed inserito come carattere nella stringa
    crea_pack.nome[strlen(crea_pack.nome) - 1] = 0;

    //Inserimento del cognome
    printf("Inserisci il cognome: ");
    if (fgets(crea_pack.cognome, BUFF_MAX_SIZE, stdin) == NULL) {
        perror("fgets() error");
    }
    //inserimento del terminatore al posto dell'invio inserito dalla fgets, poichè questo veniva contato ed inserito come carattere nella stringa
    crea_pack.cognome[strlen(crea_pack.cognome) - 1] = 0;

    //Inserimento del codice fiscale della tessera sanitaria
    while (1) {
        printf("Inserisci il codice della tessera sanitaria. Attenzione! Devi inserire esattamente 16 caratteri: ");
        if (fgets(crea_pack.cod_fisc, BUFF_MAX_SIZE, stdin) == NULL) {
            perror("fgets() error");
            exit(1);
        }

        //Controllo sull'input dell'Utente
        if (strlen(crea_pack.cod_fisc) != COD_SIZE) printf("Il numero dei caratteri della tessera sanitaria e' errato. Riprovare\n\n");
        else {

            //inserimento del terminatore al posto dell'invio inserito dalla fgets, poichè questo veniva contato ed inserito come carattere nella stringa
            crea_pack.cod_fisc[COD_SIZE - 1] = 0;
           break;
        }
    }
    return crea_pack;
}

int main(int argc, char **argv) {
    int sock_fd, benvenuto, dim_pacchetto;
    struct sockaddr_in serveraddr;
    VACCINAZIONE pacchetto;
    char buffer[BUFF_MAX_SIZE];
    char **alias;
    char *addr;
	struct hostent *data; //struttura per utilizzare la gethostbyname

    if (argc != 2) {
        perror("usage: <host name>"); //perror: Produce un messaggio sullo standard error che descrive l’ultimo errore avvenuto durante una System call o una funzione di libreria.
        exit(1);
    }

    //Creazione del descrittore del socket
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket() error");
        exit(1);
    }

    
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(1024);

    //Conversione dal nome al dominio a indirizzo IP
    if ((data = gethostbyname(argv[1])) == NULL) {
        herror("gethostbyname() error");
		exit(1);
    }
	alias = data -> h_addr_list;

    //inet_ntop converte un indirizzo in una stringa
    if ((addr = (char *)inet_ntop(data -> h_addrtype, *alias, buffer, sizeof(buffer))) < 0) {
        perror("inet_ntop() error");
        exit(1);
    }

    //Conversione dell’indirizzo IP in un indirizzo di rete in network order
    if (inet_pton(AF_INET, addr, &serveraddr.sin_addr) <= 0) {
        perror("inet_pton() error");
        exit(1);
    }

    //Connessione con il server
    if (connect(sock_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("connect() error");
        exit(1);
    }
    //FullRead per leggere quanti byte invia il Centro Vaccinale
    if (full_read(sock_fd, &benvenuto, sizeof(int)) < 0) {
        perror("full_read() error");
        exit(1);
    }
    //Ricezione del benevenuto dal CentroVaccinale
    if (full_read(sock_fd, buffer, benvenuto) < 0) {
        perror("full_read() error");
        exit(1);
    }
    printf("%s\n", buffer);

    //Creazione del pacchetto da inviare al Centro Vaccinale
    pacchetto = crea_pacchetto();

    //Invio del pacchetto richiesto al Centro Vaccinale
    if (full_write(sock_fd, &pacchetto, sizeof(pacchetto)) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Ricezione dell'ack
    if (full_read(sock_fd, buffer, ACK_SIZE) < 0) {
        perror("full_read() error");
        exit(1);
    }
    printf("%s\n\n", buffer);

    exit(0);
}