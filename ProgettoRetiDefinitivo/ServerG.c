#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>      // libreria standard del C per la gestione delle situazioni di errore
#include <string.h>
#include <netdb.h>      // contiene le definizioni per le operazioni del database di rete
#include <sys/types.h>
#include <sys/socket.h> //contiene le definizioni dei socket
#include <arpa/inet.h>  // contiene le definizioni per le operazioni Internet
#include <time.h>
#include <signal.h>     //contiene le costanti per la gestione dei segnali fra processi
#define BUFF_MAX_SIZE 1024  //dimensione massima del buffer
#define BENVENUTO 108	// dimensione del messaggio di benvenuto
#define COD_SIZE 17    //dimensione del codice di tessera sanitaria 16 byte ed 1 del terminatore
#define ACK_SIZE 64
#define ACK_SIZE_CT 60

//Struct che permette di salvare una data
typedef struct {
    int giorno;
    int mese;
    int anno;
} DATE;

//Struct del pacchetto inviato dal Centro Vaccinale al Server Vaccinale avente il codice fiscale della tessera sanitaria dell'Utente e la data di inizio e fine validità del Green Pass
typedef struct {
    char cod_fisc[COD_SIZE];
    char report;
    DATE data_inizio;
    DATE data_fine;
} GP;

//Struct del pacchetto inviato dal Client T 
typedef struct  {
    char cod_fisc[COD_SIZE];    //codice fiscale tessera sanitaria
    char report;		 //referto di validità del Green Pass
} REPORT;

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
            if (errno == EINTR) continue; // errno=codice di errore dell'ultima System Call
            else exit(n_written); //Se non è una System Call, esci con un errore
        }
        n_left -= n_written;
        buffer += n_written;
    }
    buffer = 0;
    return n_left;
}


//Handler che cattura il segnale CTRL-C e stampa un messaggio di arrivederci.
void handler (int sign){
    if (sign == SIGINT) {
        printf("\nUscita in corso...\n");
        sleep(2);
        printf("Grazie, arrivederci\n");

        exit(0);
    }
}



//Funzione per estrapolare la data corrente del sistema, usata per effettuare le operazioni di verifica del Green Pass
void creazione_dc(DATE *data_inizio) {
    time_t ticks;
    ticks = time(NULL);

    //Dichiarazione strutture per la conversione della data da stringa ad intero
    struct tm *data_i = localtime(&ticks);
    data_i->tm_mon += 1;           //Sommiamo 1 perchè i mesi vanno da 0 ad 11
    data_i->tm_year += 1900;       //Sommiamo 1900 perchè gli anni partono dal 122 (2022 - 1900)

    //Assegnamo i valori ai parametri di ritorno
    data_inizio->giorno = data_i->tm_mday ;
    data_inizio->mese = data_i->tm_mon;
    data_inizio->anno = data_i->tm_year;
}

 //Funzione per la verifica del Green Pass. Riceve un codice fiscale della tessera sanitaria dal Client S, chiede al ServerV il report 
 //ed infine comunica l'esito al Client S

char verifica_cd(char cod_fisc[]) {
    int sock_fd, benvenuto, dim_pacchetto;
    struct sockaddr_in serveraddr;
    char buffer[BUFF_MAX_SIZE], report, bit;
    GP greenP;
    DATE data_corrente;

//Inizializziamo il bit a 0 per notificare al ServerV che la comunicazione deve avvenire con il ServerG
    bit = '0';

    //Creazione del descrittore del socket
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        exit(1);
    }

    
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(1025);

     //Conversione dell’indirizzo IP in un indirizzo di rete in network order.
    if (inet_pton(AF_INET, "127.0.0.1", &serveraddr.sin_addr) <= 0) {
        perror("inet_pton error");
        exit(1);
    }

    //Connessione con il server
    if (connect(sock_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("connect() error");
        exit(1);
    }

    //Invia un bit di valore 0 al ServerV per notificarlo che la comunicazione deve avvenire con il ServerG
    if (full_write(sock_fd, &bit, sizeof(char)) < 0) {
        perror("full_write() error");
        exit(1);
    }

    bit = '1';

    //Invia un bit di valore 1 al ServerV affichè effettui la verifica del Green Pass
    if (full_write(sock_fd, &bit, sizeof(char)) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Invia il codice fiscale della tessera sanitaria ricevuto dal ClientS al SeverV
    if (full_write(sock_fd, cod_fisc, COD_SIZE)) {
        perror("full_write() error");
        exit(1);
    }

    //Ricezione del report dal ServerV
    if (full_read(sock_fd, &report, sizeof(char)) < 0) {
        perror("full_read() error");
        exit(1);
    }

    if (report == '1') {
        //Ricezione dell'esito della verifica dal ServerV, se 0 non valido se 1 valido
        if (full_read(sock_fd, &greenP, sizeof(GP)) < 0) {
            perror("full_read() error");
            exit(1);
        }

        close(sock_fd);

        //Funzione per ricavare la data corrente
        creazione_dc(&data_corrente);

     	//Se l'anno corrente è maggiore dell'anno di scadenza del Green Pass, quest'ultimo non è valido dunque assegniamo al report il valore di 0
         //Se l'anno della scadenza del Green Pass è valido ma il mese corrente è maggiore del mese di scadenza, quest'ultimo non è valido e assegniamo al report il valore di 0
         //Se l'anno e il mese della scadenza del Green Pass sono validi ma il giorno corrente è maggiore del giorno di scadenza, quest'ultimo non è valido e assegniamo al report il valore di 0
        
        if (data_corrente.anno > greenP.data_fine.anno) report = '0';
        if (report == '1' && data_corrente.mese > greenP.data_fine.mese) report = '0';
        if (report == '1' && data_corrente.giorno > greenP.data_fine.giorno) report = '0';
        if (report == '1' && greenP.report == '0') report = '0'; //Se il Green Pass è valido temporalmente MA il report (esito del tampone) è negativo, allora il GP non è valido
    }

    return report;
}

//Funzione che gestisce la comunicazione con l'Utente
void ricezione_cd(int connectfd) {
    char report, buffer[BUFF_MAX_SIZE], cod_fisc[COD_SIZE];
    int index, benvenuto, dim_pacchetto;

    //Stampa del messaggo di benvenuto da inviare al ClientS quando si connette ServerG
    snprintf(buffer, BENVENUTO, "--- Benvenuto nel ServerG ---\nInserire il codice fiscale della tessera per verificarne la validità");
    buffer[BENVENUTO - 1] = 0;
    if(full_write(connectfd, buffer, BENVENUTO) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Ricezione del codice fiscale dal Client S
    if(full_read(connectfd, cod_fisc, COD_SIZE) < 0) {
        perror("full_read error");
        exit(1);
    }

    //Notifica della corretta ricezione dei dati
    snprintf(buffer, ACK_SIZE, "I dati sono stati ricevuti correttamente!");
    buffer[ACK_SIZE - 1] = 0;
    if(full_write(connectfd, buffer, ACK_SIZE) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Funzione che invia il codice fiscale della tessera sanitaria al ServerV, riceve l'esito da quest'ultimo ed infine lo invia al Client S
    report = verifica_cd(cod_fisc);

    //Invio del report di validità del Green Pass al Client S
    if (report == '1') {
        strcpy(buffer, "Il Green Pass è valido, operazione terminata!");
        if(full_write(connectfd, buffer, ACK_SIZE_CT) < 0) {
            perror("full_write() error");
            exit(1);
        }
    } else if (report == '0') {
        strcpy(buffer, "Il Green Pass non è valido, operazione terminata!");
        if(full_write(connectfd, buffer, ACK_SIZE_CT) < 0) {
            perror("full_write() error");
            exit(1);
        }
    } else {
        strcpy(buffer, "Il codice fiscale della tessera sanitaria è inesistente");
        if(full_write(connectfd, buffer, ACK_SIZE_CT) < 0) {
            perror("full_write() error");
            exit(1);
        }
    }

    close(connectfd);
}

char invio_report(REPORT pacchetto) {
    int sock_fd;
    struct sockaddr_in serveraddr;
    char bit, buffer[BUFF_MAX_SIZE], report;

    bit = '0';

    //Creazione del descrittore del socket
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket() error");
        exit(1);
    }

    
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(1025);

    //Conversione dell'indirizzo IP dal formato dotted decimal a stringa di bit
    if (inet_pton(AF_INET, "127.0.0.1", &serveraddr.sin_addr) <= 0) {
        perror("inet_pton() error");
        exit(1);
    }

    //Connessione con il server
    if (connect(sock_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("connect() error");
        exit(1);
    }

    //Invia un bit di valore 0 al ServerV per notificarlo che deve comunicare con il ServerV
    if (full_write(sock_fd, &bit, sizeof(char)) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Invia un bit di valore 0 al ServerV per notificarlo che deve effettuare la modifica del report del Green Pass
    if (full_write(sock_fd, &bit, sizeof(char)) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Invio del pacchetto ricevuto dal ClientT al ServerV
    if (full_write(sock_fd, &pacchetto, sizeof(REPORT)) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Ricezione del report dal ServerG
    if (full_read(sock_fd, &report, sizeof(report)) < 0) {
        perror("full_read() error");
        exit(1);
    }

    close(sock_fd);

    return report;
}

void ricezione_report(int connectfd) {
    REPORT pacchetto;
    char report, buffer[BUFF_MAX_SIZE];


   //Lettura dei dati del pacchetto REPORT inviato dal ClientT
    if (full_read(connectfd, &pacchetto, sizeof(REPORT)) < 0) {
        perror("full_read() error");
        exit(1);
    }

    report = invio_report(pacchetto);

    if (report == '1') {
        strcpy(buffer, "Il codice fiscale della tessera sanitaria è inesistente");
        if(full_write(connectfd, buffer, ACK_SIZE_CT) < 0) {
            perror("full_write() error");
            exit(1);
        }
    } else {
        strcpy(buffer, "--- Operazione conclusa con successo ---");
        if(full_write(connectfd, buffer, ACK_SIZE_CT) < 0) {
            perror("full_write() error");
            exit(1);
        }
    }
}

int main() {
    int listenfd, connectfd;
    struct sockaddr_in servaddr;
    pid_t pid;
    char bit;

    signal(SIGINT,handler); //Cattura il segnale CTRL-C
    
//Creazione descrizione del socket
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket() error");
        exit(1);
    }

    
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(1026);

    //Assegnazione della porta al server
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind() error");
        exit(1);
    }

    //Mette il socket in modalità di ascolto in attesa di nuove connessioni
    if (listen(listenfd, 1024) < 0) {
        perror("listen() error");
        exit(1);
    }

    for (;;) {
    printf("In attesa di Green Pass da verificare\n");


        //Accetta una nuova connessione
        if ((connectfd = accept(listenfd, (struct sockaddr *)NULL, NULL)) < 0) {
            perror("accept() error");
            exit(1);
        }

        //Creazione del processo figlio;
        if ((pid = fork()) < 0) {
            perror("fork() error");
            exit(1);
        }

        if (pid == 0) {
            close(listenfd);

            

		// Il ServerG riceve come primo messaggio un bit , il quale può assumere come valori 0 o 1, per distinguere due connessioni diverse
       		// Se riceve 1, il processo figlio gestirà la connessione con il Client T
       		// Se riceve 0, allora il processo figlio gestirà la connessione con il Client S

            if (full_read(connectfd, &bit, sizeof(char)) < 0) {
                perror("full_read() error");
                exit(1);
            }
            if (bit == '1') ricezione_report(connectfd);   //Ricezione delle informazioni dal ClientT
            else if (bit == '0') ricezione_cd(connectfd);  //Ricezione delle informazioni dal ClientS
            else printf("Client non riconosciuto\n");

            close(connectfd);
            exit(0);
        } else close(connectfd);
    }
    exit(0);
}