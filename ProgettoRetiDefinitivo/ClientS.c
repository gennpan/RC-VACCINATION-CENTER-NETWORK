#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>      // libreria standard del C per la gestione delle situazioni di errore
#include <string.h>
#include <netdb.h>      // contiene le definizioni per le operazioni del database di rete
#include <sys/types.h>
#include <sys/socket.h> //contiene le definizioni dei socket
#include <arpa/inet.h>  // contiene le definizioni per le operazioni Internet
#define BUFF_MAX_SIZE 1024   //dimensione massima del buffer
#define ACK_SIZE_SG 64     //dimensione dell'ack ricevuto dal ServerG
#define BENVENUTO 108 //dimensione del messaggio di benvenuto 
#define ACK_SIZE_CS 60       //dimensione dell'ack ricevuto dal ClientS
#define COD_SIZE 17 //16 byte per il codice della tessera sanitaria e 1 byte per il carattere terminatore


//Legge esattamente count byte s iterando opportunamente le letture
ssize_t full_read(int fd, void *buffer, size_t count) {
    size_t n_left;
    ssize_t n_read;
    n_left = count;
    while (n_left > 0) {  // ciclo ripetuto fintanto che non vengono letti tutti i bytes
        if ((n_read = read(fd, buffer, n_left)) < 0) {
            if (errno == EINTR) continue; // errno=codice di errore dell'ultima System Call
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
            if (errno == EINTR) continue; //errno=codice di errore dell'ultima System Call
            else exit(n_written); //Se non è una System Call, esci con un errore
        }
        n_left -= n_written;
        buffer += n_written;
    }
    buffer = 0;
    return n_left;
}



int main() {
    int sock_fd;
    struct sockaddr_in serveraddr;
    char bit, report, buffer[BUFF_MAX_SIZE], cod_fisc[COD_SIZE];

    bit = '0'; //Inizializzazione del bit a 0 per inviarlo al ServerG

    //Creazione del descrittore del socket
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket() error");
        exit(1);
    }

    
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(1026);

    //Conversione dell’indirizzo IP in un indirizzo di rete in network order.
    if (inet_pton(AF_INET, "127.0.0.1", &serveraddr.sin_addr) <= 0) {
        perror("inet_pton() error");
        exit(1);
    }

    //Connessione con il server
    if (connect(sock_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("connect() error");
        exit(1);
    }

    //Invia un bit di valore 0 al ServerG per notificare che la comunicazione deve avvenire con il ClientS
    if (full_write(sock_fd, &bit, sizeof(char)) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Ricezione del benvenuto dal ServerG
    if (full_read(sock_fd, buffer, BENVENUTO) < 0) {
        perror("full_read() error");
        exit(1);
    }
    printf("%s\n\n", buffer);

    //Inserimento del codice fiscale della tessera sanitaria
    while (1) {
        printf("Inserisci codice tessera sanitaria [N.B. immettere esattamente 16 caratteri]: ");
        if (fgets(cod_fisc, BUFF_MAX_SIZE, stdin) == NULL) {
            perror("fgets() error");
            exit(1);
        }
        //Controllo sull'input dell'utente
        if (strlen(cod_fisc) != COD_SIZE) printf("Numero caratteri non corretto! Riprovare\n\n");
        else {

            //Inseriamo il terminatore al posto dell'invio, poichè veniva considerato ed inserito come carattere nella stringa
            cod_fisc[COD_SIZE - 1] = 0;
            break;
        }
    }

    //Invio del numero di tessera sanitaria al ServerG
    if (full_write(sock_fd, cod_fisc, COD_SIZE)) {
        perror("full_write() error");
        exit(1);
    }

    //Ricezione dell'ack
    if (full_read(sock_fd, buffer, ACK_SIZE_SG) < 0) {
        perror("full_read() error");
        exit(1);
    }
    printf("\n%s\n\n", buffer);

    printf("Caricamento in corso...\n\n");

    //Attesa di 4 secondi per il completamento dell'operazione
    sleep(4);
    
    //Ricezione ACK dal ServerG
    if (full_read(sock_fd, buffer, ACK_SIZE_CS) < 0) {
        perror("full_read() error");
        exit(1);
    }
    printf("%s\n", buffer);

    close(sock_fd);

    exit(0);
}