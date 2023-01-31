#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>      // libreria standard del C per la gestione delle situazioni di errore
#include <string.h>
#include <netdb.h>      // contiene le definizioni per le operazioni del database di rete
#include <sys/types.h>
#include <sys/socket.h> //contiene le definizioni dei socket.
#include <arpa/inet.h>  // contiene le definizioni per le operazioni Internet
#include <time.h>
#include <signal.h>     //contiene le costanti per la gestione dei segnali fra processi
#define BUFF_MAX_SIZE 1024      //dimensione massima del buffer
#define COD_SIZE 17      //16 byte per il codice della tessera sanitaria e 1 byte per il carattere terminatore
#define ACK_SIZE 64 	//dimesione dell'ACK inviato all'Utente dal Centro Vaccinale

//Struct del pacchetto che il Centro Vaccinale deve ricevere dall'Utente
typedef struct {
    char nome[BUFF_MAX_SIZE];
    char cognome[BUFF_MAX_SIZE];
    char cod_fisc[COD_SIZE];
} VACCINAZIONE;

//Struct che permette di salvare una data
typedef struct {
    int giorno;
    int mese;
    int anno;
} DATE;

//Struct del pacchetto inviato dal Centro Vaccinale al ServerV 
typedef struct {
    char cod_fisc[COD_SIZE];
    DATE data_inizio;	//data di inizio validità del Green Pass
    DATE data_fine;		//data di fine validità del Green Pass
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
            else exit(n_written);	 //Se non è una System Call, esci con un errore
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
        printf("Grazie, arrivedrci\n");

        exit(0);
    }
}

//Funzione per il calcolo della data di scadenza e della data di inizio validità del Green Pass

void creazione_df(DATE *data_fine) {
    time_t ticks;   //struttura per la gestione della data
    ticks = time(NULL); //Estrapoliamo l'ora esatta della macchina e lo assegniamo alla variabile

    //Dichiarazione strutture per la conversione della data da stringa ad intero
    struct tm *data_f = localtime(&ticks);
    data_f->tm_mon += 5;           //Sommiamo 5 perchè i mesi vanno da 0 ad 11 (cioè 4 mesi di scadenza)
    data_f->tm_year += 1900;       //Sommiamo 1900 perchè gli anni partono dal 122 (2022 - 1900)

    //Effettuiamo il controllo nel caso in cui il vaccino sia stato fatto nel mese di settembre, ottobre, novembre o dicembre, comportando un aumento dell'anno
    if (data_f->tm_mon == 13) { //se 13 è settembre si incrementano 4 mesi, arrivando a gennaio dell'anno successivo
        data_f->tm_mon = 1;
        data_f->tm_year++;
    }
    if (data_f->tm_mon == 14) { //se 14 è ottobre si incrementano 4 mesi, arrivando a febbraio dell'anno successivo
        data_f->tm_mon = 2;
        data_f->tm_year++;
    }
    if (data_f->tm_mon == 15) { //if 15 è novembre quindi si incrementano 4 mesi, arrivando a marzo dell'anno successivo
        data_f->tm_mon = 3;
        data_f->tm_year++;
    }
    if (data_f->tm_mon == 16) { //if 16 è dicembre quindi si incrementano 4 mesi, arrivando a aprile dell'anno successivo
        data_f->tm_mon = 4;
        data_f->tm_year++;
    }

    printf("La data di fine validità del Green Pass e': %02d:%02d:%02d\n", data_f->tm_mday, data_f->tm_mon, data_f->tm_year);

    //Assegnamo i valori ai parametri di ritorno
    data_fine->giorno = data_f->tm_mday ;
    data_fine->mese = data_f->tm_mon;
    data_fine->anno = data_f->tm_year;
}

//Funzione per calcolare la data di inizio validità del GP, cioè la data esatta in cui il certificato viene emesso
void creazione_di(DATE *data_inizio) {
    time_t ticks;
    ticks = time(NULL);

    //Dichiarazione strutture per la conversione della data da stringa ad intero
    struct tm *data_i = localtime(&ticks);
    data_i->tm_mon += 1;           //Sommiamo 1 perchè i mesi vanno da 0 ad 11
    data_i->tm_year += 1900;       //Sommiamo 1900 perchè gli anni partono dal 122 (2022 - 1900)

    printf("La data di inizio validità del green pass e': %02d:%02d:%02d\n", data_i->tm_mday, data_i->tm_mon, data_i->tm_year);

    //Assegnamo i valori ai parametri di ritorno
    data_inizio->giorno = data_i->tm_mday ;
    data_inizio->mese = data_i->tm_mon;
    data_inizio->anno = data_i->tm_year;
}


//Funzione che invia al ServerV un Green Pass con data inizio e fine validità ed infine il codice fiscale della tessera sanitaria
void invio_GP(GP greenP) {
    int sock_fd;
    struct sockaddr_in serveraddr;
    char bit, buffer[BUFF_MAX_SIZE];

    bit = '1'; //Inizializzazione del bit a 1 da inviare al ServerV

    //Creazione del descrittore del socket
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket() error");
        exit(1);
    }

    
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(1025); //porta

	//Conversione dell'indirizzo IP in un indirizzo di rete scritto in network order
    if (inet_pton(AF_INET, "127.0.0.1", &serveraddr.sin_addr) <= 0) {
        perror("inet_pton() error");
        exit(1);
    }

    // Connessione con il server
    if (connect(sock_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("connect() error");
        exit(1);
    }

    //Invia un bit di valore 1 al ServerV per notificare che la comunicazione deve avvenire con il Centro Vaccinale
    if (full_write(sock_fd, &bit, sizeof(char)) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Invo del Green Pass al ServerV
    if (full_write(sock_fd, &greenP, sizeof(greenP)) < 0) {
        perror("full_write() error");
        exit(1);
    }

    close(sock_fd);
}

    //Funzione per la gestione della comunicazione con l'Utente
void risposta_utente(int connectfd) {
    char buffer[BUFF_MAX_SIZE];
    int benvenuto, dim_pacchetto;
    VACCINAZIONE pacchetto;
    GP greenP;

    

    //Stampa del messaggo di benvenuto da inviare all'Utente quando si collega al Centro Vaccinale
    snprintf(buffer, BUFF_MAX_SIZE, "--- Benvenuto nel centro vaccinale --- \nImmettere nome, cognome e codice fiscale della tessera sanitaria per inserirli sulla piattaforma.\n");
    benvenuto = sizeof(buffer);
   
 //Invio dei bytes di scrittura del buffer
    if(full_write(connectfd, &benvenuto, sizeof(int)) < 0) {
        perror("full_write() error");
        exit(1);
    }
    
//Invio del benvenuto
    if(full_write(connectfd, buffer, benvenuto) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Riceziome delle informazioni per il Green Pass inviate dall'Utente
    if(full_read(connectfd, &pacchetto, sizeof(VACCINAZIONE)) < 0) {
        perror("full_read() error");
        exit(1);
    }

    printf("\nI dati ricevuti sono:\n");
    printf("Nome: %s\n", pacchetto.nome);
    printf("Cognome: %s\n", pacchetto.cognome);
    printf("Codice Fiscale Tessera Sanitaria: %s\n\n", pacchetto.cod_fisc);

   //Notifica della corretta ricezione dei dati inviati all'Utente
    snprintf(buffer, ACK_SIZE, "Inserimento dei dati avvenuto con successo");
    if(full_write(connectfd, buffer, ACK_SIZE) < 0) {
        perror("full_write() error");
        exit(1);
    }

    //Copia del codice fiscale della tessera sanitaria inviato dall'Utente nel Green Pass da inviare al ServerV
    strcpy(greenP.cod_fisc, pacchetto.cod_fisc);
   
 //Si ottiene la data di inizo validità del Green Pass
    creazione_di(&greenP.data_inizio);

    //Creazione della data di scadenza (4 mesi)
    creazione_df(&greenP.data_fine);

    close(connectfd);

    //Invio del nuovo Green Pass al CentroVaccinale
    invio_GP(greenP);
}

int main(int argc, char const *argv[]) {
    int listenfd, connectfd;
    VACCINAZIONE pacchetto;
    struct sockaddr_in servaddr;
    pid_t pid;
    signal(SIGINT,handler); //Cattura il segnale CTRL-C
    
//Creazione descrizione del socket
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket() error");
        exit(1);
    }

    
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(1024);

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

    printf("In attesa di nuove domande per la vaccinazione\n");

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

            //Ricezione delle informazioni dall'Utente
            risposta_utente(connectfd);

            close(connectfd);
            exit(0);
        } else close(connectfd);
    }
    exit(0);
}