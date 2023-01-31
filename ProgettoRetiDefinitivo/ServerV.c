#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>      // libreria standard del C per la gestione delle situazioni di errore.
#include <string.h>
#include <fcntl.h>      // contiene opzioni di controllo dei file
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/socket.h> //contiene le definizioni dei socket.
#include <arpa/inet.h>  // contiene le definizioni per le operazioni Internet.
#include <time.h>
#include <signal.h>     //contiene le costanti per la gestione dei segnali fra processi.
#define BUFF_MAX_SIZE 2048   //dimensione massima del buffer
#define COD_SIZE 17        //dimensione del codice di tessera sanitaria 16 byte ed 1 del terminatore

//Struct del pacchetto del ClientT 
typedef struct  {
    char cod_fisc[COD_SIZE]; 			//codice della tessera sanitaria
    char report;				//referto di validità del Green Pass
} REPORT;

//Struct che permette di salvare una data
typedef struct {
    int giorno;
    int mese;
    int anno;
} DATE;

//Struct del pacchetto inviato dal Centro Vaccinale al ServerV
typedef struct {
    char cod_fisc[COD_SIZE];
    char report; //0 Green Pass non valido, 1 Green Pass valido
    DATE data_inizio;		//data di inizio validità del Green Pass
    DATE data_fine;			//data di fine validità del Green Pass
} GP;

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



//Handler che cattura il segnale CTRL-C e stampa un messaggio di arrivederci.
void handler (int sign){
    if (sign == SIGINT) {
        printf("\nUscita in corso...\n");
        sleep(2); //attende 2 secondi prima della prossima operazione
        printf("***Grazie per aver utilizzato il nostro servizio***\n");
        exit(0);
    }
}

//Funzione che invia un GP richiesto dal ServerG
void invio_gp(int connectfd) {
    char report, cod_fisc[COD_SIZE];
    int fd;
    GP greenP;

    //Riceve il codice della tessera sanitaria dal ServerG
    if (full_read(connectfd, cod_fisc, COD_SIZE) < 0) {
        perror("full_read() error");
        exit(1);
    }

    //Apre il file rinominato "cod_fisc", cioè il codice ricevuto dal ServerG
    fd = open(cod_fisc, O_RDONLY, 0777);


    // Se il codice della tessera sanitaria inviato dal Client T non esiste, la variabile errno cattura l'evento ed 
       // invierà un report uguale ad 1 al ServerG, il quale aggiornerà il Client T dell'inesistenza del codice fiscale 
       // altrimenti invierà un report uguale a 0 per indicare che l'operazione è avvenuta correttamente


    if (errno == 2) {
        printf("Il codice della tessera sanitaria è inesistente, riprovare\n");
        report = '2';
        
	//Invia il report al ServerG
        if (full_write(connectfd, &report, sizeof(char)) < 0) {
            perror("full_write() error");
            exit(1);
        }
    } else {

        //Accesso in mutua esclusione al file in lettura
        
		if (flock(fd, LOCK_EX) < 0) {
            perror("flock() error");
            exit(1);
        }

        //Lettura del Green Pass dal file 
        
		if (read(fd, &greenP, sizeof(GP)) < 0) {
            perror("read() error");
            exit(1);
        }

        //Sblocchiamo il lock
        
		if(flock(fd, LOCK_UN) < 0) {
            perror("flock() error");
            exit(1);
        }

        close(fd);
        report = '1';

        //Invia il report al ServerG
        
		if (full_write(connectfd, &report, sizeof(char)) < 0) {
            perror("full_write() error");
            exit(1);
        }

        //Invio del Green Pass richiesto al ServerG il quale controllerà la sua validità
        
		if(full_write(connectfd, &greenP, sizeof(GP)) < 0) {
            perror("full_write() error");
            exit(1);
        }
    }
}


//Funzione per la modifica del report di un Green Pass richiesto dal ClientT
void modifica_report(int connectfd) {
    REPORT pacchetto;
    GP greenP;
    int fd;
    char report;

    //Riceve il pacchetto dal ServerG ottenuto dal Client T avente il codice fiscale della tessera sanitaria ed il referto del tampone
    if (full_read(connectfd, &pacchetto, sizeof(REPORT)) < 0) {
        perror("full_read() error");
        exit(1);
    }

    //Apertura del file contenente il Green Pass relativo al codice fiscale della tessera ricevuto dal Client T
    fd = open(pacchetto.cod_fisc, O_RDWR , 0777);

   // Se il codice fiscale della tessera sanitaria proveniente dal Client T è inesistente, la variabile errno cattura l'evento ed 
        // invierà un report uguale ad 1 al ServerG, il quale aggiornerà il Client T dell'inesistenza del codice fiscale 
        // altrimenti invierà un report uguale a 0 per indicare che l'operazione è avvenuta correttamente

    if (errno == 2) {
        printf("Il codice della tessera sanitaria è inesistente, riprovare!\n");
        report = '1';
    } else {

        //Accesso in mutua esclusione al file in lettura
        if(flock(fd, LOCK_EX | LOCK_NB) < 0) {
            perror("flock() error");
            exit(1);
        }

        //Lettura del file contenente il Green Pass associato al codice fiscale della tessera sanitaria ricevuto dal Client T
        if (read(fd, &greenP, sizeof(GP)) < 0) {
            perror("read() error");
            exit(1);
        }

        //Assegnazione del report ricevuto dal Client T al Green Pass corrispondente
        greenP.report = pacchetto.report;

        //Ci posizioniamo all'inizio dello stream del file
        lseek(fd, 0, SEEK_SET);

        //Sovrascrittura dei campi del Green Pass nel file binario 
        if (write(fd, &greenP, sizeof(GP)) < 0) {
            perror("write() error");
            exit(1);
        }

        //Sblocchiamo il lock
        if(flock(fd, LOCK_UN) < 0) {
            perror("flock() error");
            exit(1);
        }
        report = '0';
    }

    //Invia il report al ServerG
    if (full_write(connectfd, &report, sizeof(char)) < 0) {
        perror("full_write() error");
        exit(1);
    }
}


  //Funzione che gestisce la comunicazione con il ServerG: Estrae il Green Pass associato al relativo codice fiscale della tessera saniteria ricevuto dal file system e lo invia al ServerG

void comunicazione_SV(int connectfd) {
    char bit;

       // Il ServerV riceve un bit dal ServerG, il quale può assumere come valori 0 o 1, per distinguere due operazioni diverse
       // Se riceve 0, il ServerV gestirà l'operazione per la modifica del referto di un Green Pass
       // Se riceve 1, il ServerV gestirà l'operazione per l'invio di un Green Pass al ServerG
    

    if (full_read(connectfd, &bit, sizeof(char)) < 0) {
        perror("full_read() error");
        exit(1);
    }
    if (bit == '0') modifica_report(connectfd);
    else if (bit == '1') invio_gp(connectfd);
    else printf("Dato non valido\n\n");
}

//Funzione che gestisce la comunicazione con il Centro Vaccinale. Inoltre salva i dati ricevuti dal Centro Vaccinale in un filesystem
void comunicazione_CV(int connectfd) {
    int fd;
    GP greenP;

    //Ricezione del Green Pass dal Centro Vaccinale
    if (full_read(connectfd, &greenP, sizeof(GP)) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Un Green Pass appena generato è valido di default
    greenP.report = '1';

    //Si crea un file contenente i dati ricevuti per ogni tessera sanitaria 
    if ((fd = open(greenP.cod_fisc, O_WRONLY| O_CREAT | O_TRUNC, 0777)) < 0) {
        perror("open() error");
        exit(1);
    }

	//Scrittura dei campi del Green Pass nel file binario    
     if (write(fd, &greenP, sizeof(GP)) < 0) {
        perror("write() error");
        exit(1);
    }

    close(fd);
}

int main() {
    int listenfd, connectfd, dim_pacchetto;
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
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); //INADDR_ANY: Viene utilizzato come indirizzo del server, l’applicazione accetterà connessioni da qualsiasi indirizzo associato al server.
    servaddr.sin_port = htons(1025);

    //Mette il socket in modalità di ascolto in attesa di nuove connessioni
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind() error");
        exit(1);
    }

    //Mette il socket in ascolto in attesa di nuove connessioni
    if (listen(listenfd, 1024) < 0) {
        perror("listen() error");
        exit(1);
    }

    for (;;) {

    printf("In attesa di nuovi dati\n\n");

        //Accetta una nuova connessione
        if ((connectfd = accept(listenfd, (struct sockaddr *)NULL, NULL)) < 0) {
            perror("accept() error");
            exit(1);
        }

        //Creazione del processo figlio
        if ((pid = fork()) < 0) {
            perror("fork() error");
            exit(1);
        }

        //Codice eseguito dal processo figlio
        if (pid == 0) {
            close(listenfd);

                // Il ServerV riceve come primo messaggio un bit, il quale può assumere come valori 0 o 1, per distinguere due connessioni diverse
                // Se riceve 1, il processo figlio gestirà la connessione con il Centro Vaccinale
                // Invece se riceve 0, il processo figlio gestirà la connessione con il ServerV

            if (full_read(connectfd, &bit, sizeof(char)) < 0) {
                perror("full_read() error");
                exit(1);
            }
            if (bit == '1') comunicazione_CV(connectfd);
            else if (bit == '0') comunicazione_SV(connectfd);
            else printf("Client inesistente!\n\n");

            close(connectfd);
            exit(0);
        } else close(connectfd); //Codice eseguito dal processo padre
    }
    exit(0);
}