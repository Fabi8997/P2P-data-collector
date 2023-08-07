#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

//lunghezza massima buffer temporaneo
#define BUFFER_SIZE 1024

//lunghezza massima comando passato da stdin
#define COMMAND_SIZE 20

#define STDIN 0

#define REQ_LEN 9//BOOT_REQ/0  DISC_REQ/0  AGG_NEIG/0  DS__EXIT/0

//porta del discovery server
#define POLLING_TIME 3

//lunghezza data  dd:mm:yyyy\0
#define DATE_LEN 12

#define ACK_LEN  4  //ACK/0

//porta del discovery server
int portaDS;

//variabili d'appoggio
char buffer[BUFFER_SIZE];
char comando[COMMAND_SIZE];
char opzione1[COMMAND_SIZE];

//non sono state usate
int portaMin;
int portaMax;

//Struttura che descrive una porta all'interno del Registro delle presenze di un determinato giorno
struct id{

    //porta presente
    int porta;

    //puntatore per costruire una lista di presenze
    struct id* next;
};

//Struttura che contiene la data relativa ad un giorno e una lista di interi (numeri di porta) dei peer presenti in quel giorno
struct presenze{
    //Data del giorno considerato
    char data[12];

    //Elenco presenti
    struct id* listaPresenze;

    //Prossimo elemento del registro
    struct presenze* next;
};

//Struttura che descrive un peer connesso
struct peer{
    char* ip;
    int porta;
    //variabili che tengono la porta dei neighbors
    int neighbor1;
    int neighbor2;
    //primo nella coda di descrittori di peer
    int primo;
    //ultimo nella coda di descrittori di peer
    int ultimo;
    //variabile per sapere se tale descrittore ha avuto i i neighbor modificati
    //ed aggiornare tramite un messaggio i neighbor dei peer
    int modificato;
    //puntatore al prossimo descrittore in coda
    struct peer* next;
};

//Registro delle presenze registrate dal boot del DS alla data odierna
struct presenze* RegPresenze = NULL;

//Registro contenente i peer attualmente connessi 
struct peer* coda;

//Contiene il numero di peer attualmente connessi
int numConnessi = 0;

//ora corrente
int ora;

//Variabile per gestire la chiusura alle 18
int chiuso = 0;

// Inserisce i peer ordinati per numero di porta ed aggiorna il numero di connessi
void inserisciPeerOrd(struct peer* elemento){

 
    struct peer* work;
    struct peer* s;

    if(coda == NULL){
        elemento->next = NULL;
        elemento->neighbor1 = 0;
        elemento->neighbor2 = 0;
        coda = elemento;
        //printf("Primo elemento della lista inserito!\n");
        return;
    }
    
    if(coda->porta >= elemento->porta){

       elemento->neighbor2 = coda->porta;
       coda->neighbor1 = elemento->porta;
       coda->modificato = 1;
      
       if(coda->next != NULL){
           coda->neighbor2 = coda->next->porta;
           elemento->neighbor1 = coda->next->porta;
           if(coda->next->next == NULL){
               coda->next->neighbor1 = coda->porta;
               coda->next->neighbor2 = elemento->porta;
               coda->next->modificato = 1;
           }
           coda->next->modificato = 1;
       }else{
           elemento->neighbor1 = 0;
       }
       work = coda;
       coda = elemento;
       elemento->next = work;
       //printf("Inserimento in testa\n");  //DEBUG
       return;
    }

    s = coda;
    while(s->next != 0){
        if(s->next->porta >= elemento->porta){

            elemento->neighbor1 = s->porta;
            elemento->neighbor2 = s->next->porta;
            s->neighbor2 = elemento->porta;
            s->modificato = 1;
            s->next->neighbor1 = elemento->porta;
            s->next->modificato = 1;

            //inserimento come secondo
            if(s->porta == coda->porta){
                coda->neighbor2 = elemento->porta;
                coda->neighbor1 = elemento->neighbor2;
                coda->modificato = 1;
            //inserimento come terzo
            }else if(s->neighbor1 == coda->porta){
                coda->neighbor1 = elemento->porta;
                coda->modificato = 1;
            }
            
            //inserimento come penultimo
            if(s->next->next == NULL){
                s->next->neighbor2 = s->porta;
            //inserimento come terzultimo
            }else if(s->next->next->next == NULL){
                s->next->next->neighbor2 = elemento->porta;
                s->next->next->modificato = 1;
            }
            
            work = s->next;
            s->next = elemento;
            elemento->next = work;
            //printf("Inserimento in mezzo\n"); //DEBUG
            return;
        }else if(s->next->next == NULL){
            s->modificato = 1;
        }
        s = s->next;
    }

    //Inserimento in coda

    elemento->neighbor1 = s->porta;
    elemento->neighbor2 = s->neighbor1;
    if(s->neighbor1 == coda->porta){
        coda->neighbor1 = s->porta;
        coda->neighbor2 = elemento->porta;
        coda->modificato = 1;
    }
    s->neighbor2 = elemento->porta;
    s->modificato = 1;
    s->next = elemento;
    elemento->next = NULL;
    //printf("Inserimento in coda\n");  //DEBUG
    numConnessi++;


}

//Stampa i peer attualmente connessi
void showPeers(){

    struct peer* s ;
    s = coda;
    printf("Elenco peer connessi:\n");
    if(s == NULL){
        printf("Nessun peer connesso\n");
        return;
    }
    while(s != NULL){
        printf("[%d]\n",s->porta);
        s = s->next;
    }
    return;
}

//Stampa tutti i peer con i relativi neighbors
void showNeighborsAll(){

    struct peer* s ;
    s = coda;
    printf("Elenco neighbors di ogni peer connesso:\n");
    if(s == NULL){
        printf("Nessun peer connesso\n");
        return;
    }
    while(s != NULL){
        printf("\nNeighbors del peer [%d]:\n",s->porta);
        printf("[Peer: %d]  [Peer: %d]\n",s->neighbor1, s->neighbor2);
        s = s->next;
    }
    return;
}

//Stampa i neighbors del peer id
void showNeighbors(int id){

    struct peer* s ;
    s = coda;
    //printf("Elenco neighbors del peer [%d]:\n",id);
    if(s == NULL){
        printf("Nessun peer connesso\n");
        return;
    }
    while(s != NULL){
        if(s->porta == id){
            printf("Neighbors del peer [%d]:\n",s->porta);
            printf("[Peer: %d]  [Peer: %d]\n",s->neighbor1, s->neighbor2);
            return;
        }
        s = s->next;
    }
    printf("Peer [%d] non trovato!\n",id);
    return;
}

//Controlla se ci sono state modifiche nel registro dei peer, restituisce la porta del primo peer modificato incontrato
int trovaMod(int* n1, int* n2, int* portaS){

    struct peer* s ;
    s = coda;
    if(s == NULL){
        printf("Nessun peer!\n");
        return 0;
    }
    while(s != NULL){
        if(s->modificato == 1){
            //printf("Trovato peer [%d] modificato con neighbors:\n",s->porta);
            //printf("[Peer: %d]  [Peer: %d]\n",s->neighbor1, s->neighbor2);
            s->modificato = 0;
            *n1 = s->neighbor1;
            *n2 = s->neighbor2;
            *portaS = s->porta;
            return 1;
        }
        s = s->next;
    }
    //printf("Nessun peer da modificare!\n");
    return 0;
}

//Stampa i comandi possibili
void stampaComandi(){

    char* comandi = "\n***************************** DS COVID STARTED ********************************\n"
                    "Digita un comando:\n"
                    "1) help --> mostra i dettagli dei comandi\n"
                    "2) showpeers --> mostra un elenco dei peer connessi\n"
                    "3) showneighbors <peer> --> mostra i neighbor di un peer\n"
                    "4) esc --> chiude il DS\n";

    printf("%s\n", comandi);
}

//Stampa l'output del comando help, una breve descrizione di ogni comando
void help(){

    char* helpComandi = "\n**************************************************************************\n"
                        "Verrà data una breve spiegazione dei comandi disponibili eccetto help:\n"
                        "comando showpeers:\n"
                        "mostra l'elenco dei peer connessi alla rete tramite il loro numero di porta\n"
                        "ES:\nElenco peer connessi:\n[4545]\n[4646]\n\n"
                        "comando showneighbors <peer> :\n"
                        "mostra i due neighbor del peer con numero di porta <peer>\n"
                        "ES:\nshowneighbors 1111\nNeighbors del peer [1111]:\n[1110]   [1222]\n\n"
                        "comando showneighbors :\n"
                        "mostra i due neighbor di tutti i peer\n"
                        "ES:\nshowneighbors \nElenco dei neighbors di ogni peer connesso\nNeighbors del peer [2222]:\n[2222]   [2333]\nNeighbors del peer [1111]:\n[1110]   [1222]\n\n"
                        "comando esc:\n"
                        "termina il DS e con esso i peer connessi al momento della sua terminazione\n"
                        "\n**************************************************************************\n";

    printf("%s", helpComandi);
}

//Estrae un peer dal registro dei peer connessi, viene dunque eliminato
void estraiPeer(int porta){

    
    struct peer* s;
    struct peer* work;

    if(coda == NULL){
        //printf("coda vuota");
        return;
    }

    numConnessi--;
    if(coda->porta == porta){
        s = coda;
        coda = coda->next;
        free(s);
        return;
    }
    work = coda;
    s = coda->next;
    while(s != NULL){
        if(s->porta == porta){
            work->next = s->next;
            
            free(s);
            return;
        }

        work = s;
        s = s->next;
        
    }
}

//Aggiorna tutti i neighbors dei peer
void aggiornaNeig(){

    struct peer* s;
    struct peer* work;

    if(coda == 0){
        //printf("coda vuota\n");
        return;
    }

    s = coda;

    while(s != 0){
        s->modificato = 1;
        s = s->next;
    }

    //AGGIORNO CODA, VALUTO 3 CASI: 1 ELEMENTO IN CODA, 2 ELEMENTI IN CODA, 3 ELEMENTI IN CODA
    if(coda->next != NULL){
        
        coda->neighbor2 = coda->next->porta;//d
        
        if(coda->next->next != NULL){
            coda->neighbor1 = coda->next->next->porta; //s
            
            if(coda->next->next->next == 0){//guardo se sono solo 3 ==> aggiorno anche i neighbor dell'ultimo
               
                coda->next->next->neighbor1 = coda->next->porta; //s
                coda->next->next->neighbor2 = coda->porta;      //d

                coda->next->neighbor2 = coda->next->next->porta; //d
                coda->next->neighbor1 = coda->porta;    //s

                return;
            }
        }else{//solo 2 peer aggiorno 
            
            coda->neighbor2 = 0;  //d
            coda->neighbor1 = coda->next->porta; //s
            coda->next->neighbor2 = coda->porta;//d
            coda->next->neighbor1 = 0;  //s
            
            return;
        }
    }else{//solo 1 peer
        coda->neighbor1 = 0;
        coda->neighbor2 = 0;
        return;
    }

    work = coda;
    s = coda->next;
    while(s->next != NULL){

        s->neighbor2 = s->next->porta; //d
        s->neighbor1 = work->porta;     //s

        //s è penultimo e con work hanno le informazioni per aggiornare l'ultimo!
        if(s->next->next == 0){
            s->next->neighbor2 = work->porta;
            s->next->neighbor1 = s->porta; //s
        }

        work = s;
        s = s->next;
    }   

    return;
}

//Ricava la data odierna aggiornata alle 18
void getDate(char* str){

    time_t rawtime;   
    char dd[3];
    char mm[3];
    char yyyy[5];
    int giorno;
    struct tm *info;
    time(&rawtime);
    
    info = localtime( &rawtime );


    if(info->tm_hour < 18){
        giorno = info->tm_mday;
    }else{
        giorno = info->tm_mday+1;
    }
    
    if(info->tm_mon+1 < 10){
        sprintf(mm,"0%d",info->tm_mon+1);
    }else{
        sprintf(mm,"%d",info->tm_mon+1);
    }
    if(giorno < 10){
        sprintf(dd,"0%d",giorno);
    }else{
        sprintf(dd,"%d",giorno);
    }
    sprintf(yyyy,"%d",info->tm_year+1900);
    
    sprintf(str,"%s:%s:%s",dd,mm,yyyy);

}

//Inserisce la presenza del peer identificato con porta al registro odierno dei presenti
void inserisciPresenza(int porta){

    //Variabile d'appoggio per la data odierna aggiornata alle 18
    char data[12];

    //struttura che verrà inserita nella lista dei presenti con id->porta = porta
    struct id* x = (struct id*)malloc(sizeof(struct id));


    getDate(data);
    
    //printf("%s\n",data);


    //Primo elemento in assoluto connesso al network
    if(RegPresenze == NULL){
        struct presenze* p = (struct presenze*)malloc(sizeof(struct presenze));
        p->next = NULL;
        p->listaPresenze = NULL;
        getDate(p->data);
        RegPresenze = p;
    }
    
    //Primo elemento del RegPresenze giornaliero
    if(strcmp(RegPresenze->data, data)){
        struct presenze* p = (struct presenze*)malloc(sizeof(struct presenze));       
        getDate(p->data);
        p->listaPresenze = NULL;
        p->next = RegPresenze;
        RegPresenze = p;
    }

    x->porta = porta;
    x->next = RegPresenze->listaPresenze;
    RegPresenze->listaPresenze = x;

    
}

//Inserisce nel registro delle presenze una presenza non registrata in data odierna
void inserisciPresenzaVecchia(int porta, char* data){


    //struttura che verrà inserita nella lista dei presenti con id->porta = porta
    struct id* x = (struct id*)malloc(sizeof(struct id));
    
    //printf("%s\n",data);


    //Primo elemento in assoluto connesso al network
    if(RegPresenze == NULL){
        struct presenze* p = (struct presenze*)malloc(sizeof(struct presenze));
        p->next = NULL;
        p->listaPresenze = NULL;
        strcpy(p->data,data);
        RegPresenze = p;
    }
    
    //Primo elemento del RegPresenze giornaliero
    if(strcmp(RegPresenze->data, data)){
        struct presenze* p = (struct presenze*)malloc(sizeof(struct presenze));       
        strcpy(p->data,data);
        p->listaPresenze = NULL;
        p->next = RegPresenze;
        RegPresenze = p;
    }

    x->porta = porta;
    x->next = RegPresenze->listaPresenze;
    RegPresenze->listaPresenze = x;

    
}

//Stampa le presenze da quando il ds è stato startato   //DEBUG
void stampaPresenze(){

    struct presenze* s;
    struct id* i;
    if(RegPresenze == NULL){
        printf("RegPresenze vuoto\n");
        return;
    }

    s = RegPresenze;

    while(s != NULL){

        printf("Presenze giorno %s:\n",s->data);
        i = s->listaPresenze;
        while(i != NULL){
            if(s->listaPresenze == NULL){
                printf("Nessun peer connesso\n");
                break;
            }
            printf("%d\n",i->porta);
            i = i->next;
        }

        s = s->next;
    }
}

//Aggiorna le presenze poichè sono scoccate le 18:00
void aggiornaPresenze(){

    //puntatore per la scansione
    struct peer* s;

    s = coda;

    while(s != NULL){

        //inserimento nel registro delle presenze odierno
        inserisciPresenza(s->porta);
        s = s->next;
    }
}

//RICAVA L'ORA CORRENTE
void getHour(int* hour){
    time_t rawtime;
    struct tm* info;
    
    time(&rawtime);
    info = localtime( &rawtime);

    *hour = info->tm_hour;
    return;
}

//Cerca la data indicata nel registro delle presenze, ritorna 0 se non la trova, 1 altrimenti
int trovaData(char* data){

    struct presenze* s;

    if(RegPresenze == 0)
        return 0;

    s = RegPresenze;

    while( s != NULL){
        if(strcmp(data,s->data) == 0){
            printf("Data trovata");
            return 1;
        }
        s = s->next;
    }

    return 0;
}


int main(int argc, char** argv){


    int ret, sd, maxfd, len;

    //Conterrà le lunghezze dei messaggi
    uint16_t lmsg;

    //Lunghezza socket con cui si vuole interagire
    socklen_t addrlen;

    //Variabili per contenere l'indirizzo di un socket
    struct sockaddr_in my_addr,connecting_addr; 

    //Variabile che conterrà i messaggi da inviare
    char msg[BUFFER_SIZE];

    //Set di lettura per la select
    fd_set read_fds;

    //Intervallo di timeout per la select, posto a 0 per renderlo non bloccante
    struct timeval timeout = {0,0};

   
    addrlen = sizeof(connecting_addr);

    //Controllo che venga passata la porta del server
    if(argc == 2){
        portaDS = atoi(argv[1]);
        printf("[DS][LOG]  Porta DS: %d\n", portaDS);
    }else{
        perror("[DS][ERR]  Non è stata inserita la porta\n");
        exit(-1);
    }

    //Inserimento delle presenze non odierne
    inserisciPresenzaVecchia(5010, "16:02:2021");
    inserisciPresenzaVecchia(5020, "17:02:2021");
    inserisciPresenzaVecchia(5030, "18:02:2021");
    inserisciPresenzaVecchia(5040, "19:02:2021");
    inserisciPresenzaVecchia(5050, "20:02:2021");
    inserisciPresenzaVecchia(5050, "21:02:2021");
    inserisciPresenzaVecchia(5040, "22:02:2021");
    inserisciPresenzaVecchia(5030, "23:02:2021");
    inserisciPresenzaVecchia(5020, "24:02:2021");
    inserisciPresenzaVecchia(5010, "15:02:2021");
    inserisciPresenzaVecchia(5020, "14:02:2021");
    inserisciPresenzaVecchia(5030, "14:02:2021");
    inserisciPresenzaVecchia(5040, "14:02:2021");
    inserisciPresenzaVecchia(5050, "13:02:2021");

    //Creazione socket UDP
    sd = socket(AF_INET,SOCK_DGRAM, 0);
    if(sd < 0){
        perror("[DS][ERR]  Errore in fase di creazione del socket: \n" );
        exit(-1);
    }else{
        printf("[DS][LOG]  Socket %d creato correttamente\n",sd);
    }
    
    //Creazione indirizzo
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET ;
    my_addr.sin_port = htons(portaDS);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    
    //Aggancio socket all'indirizzo
    ret = bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr));
    if(ret < 0){
        perror("[DS][ERR]  Errore in fase di bind: \n" );
        exit(-1);
    }else{
        printf("[DS][LOG]  Bind completata correttamente\n");
    }

    FD_ZERO(&read_fds);
    
    maxfd = (sd > STDIN)?sd:STDIN;
    maxfd++;

    //stampa i possibili comandi
    stampaComandi();
    FD_SET(sd, &read_fds);
    FD_SET(STDIN, &read_fds);
    while (1){

        //estraggo i descrittori pronti dal read_fds set
        while(select(maxfd, &read_fds, NULL, NULL, &timeout) > 0){
            
            //controllo se il descrittore pronto è sd, utilizzato per attendere BOOT_REQ
            if(FD_ISSET(sd, &read_fds)){      
                //printf("TROVATO sd pronto\n");  //DEBUG
                
                //Leggo la richiesta su sd
                do{              
                    ret = recvfrom(sd, msg, REQ_LEN, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                    //printf("ret: %d\n",ret);  //DEBUG
                    //printf("%s\n",msg);       //DEBUG
                    if(ret < 0){
                        perror("Errore nella ricezione della richiesta");
                        sleep(POLLING_TIME);
                    
                    }
                }while(ret < 0);

                //Se la richiesta è di DISC_REQ la gestisco
                if(strcmp(msg,"PRES_REQ") == 0){

                    //scorre il reg
                    struct presenze* s;
                    char str[6]; //max porta è 64535
                    char data[12]; //variabile per tenere conto della data richiesta

                    printf("[DS][LOG]  PRES_REQ individuata\n");
                    
                    //Attendo la data che sicuramente ha lunghezza uguale a 12
                    do{
                        ret = recvfrom(sd, (void*)msg, DATE_LEN , 0, (struct sockaddr*)&connecting_addr, &addrlen);

                        if(ret < 0){
                            perror("Errore nella ricezione della data");
                            sleep(POLLING_TIME);
                        }
                    }while(ret < 0);

                    strcpy(data,msg);

                    //Ricerco se tale data fa parte del registro presenze, in caso contrario non sono presenti entry
                    //if(trovaData(msg) == 1){  }

                        s = RegPresenze;

                        //Finche non terminano le presenze della data ricevuta, continuo a mandare le presenze
                        while( s != NULL){
                            if(strcmp(data,s->data) == 0){
                                printf("[DS][LOG]  Data trovata\n");
                                
                                //Variabile d'appoggio per l'invio di una presenza
                                struct id* i;
                                
                                i = s->listaPresenze;
                                
                                while(i != NULL){


                                    sprintf(str,"%d",i->porta);

                                    len = strlen(str)+1;
                                    lmsg = htons(len);

                                    //invio la lunghezza della porta
                                    do{
                                    ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                        if(ret < 0){
                                            sleep(POLLING_TIME);
                                            perror("Errore nella sendto");
                                        }
                                    }while(ret < 0 );

                                    printf("[DS][LOG]  Lunghezza porta inviata \n");

                                    //invio porta
                                    do{
                                    ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                        if(ret < 0){
                                            sleep(POLLING_TIME);
                                            perror("Errore nella sendto");
                                        }
                                    }while(ret < 0 );
                                    
                                    printf("[DS][LOG]  Porta del peer %s inviata \n", str);

                                    //attendo ack
                                    do{
                                        ret = recvfrom(sd, (void*)msg, ACK_LEN, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                            if(ret < 0){
                                                printf("[DS][ERR]  Timeout scaduto!\n");
                                                sleep(POLLING_TIME);                                   
                                                }
                                    } while (ret < 0);

                                    printf("[DS][LOG]  ACK ricevuto \n");

                                    i = i->next;
                                }

                                //esco dall'if perchè sono finiti i presenti e vado al break del while
                                break;
                            }else{
                                s = s->next;
                                continue;
                            }

                            //Esco dal while sui vari giorni
                            break;
                        }                    

                        if(s == NULL){
                            len = 0;
                            lmsg = htons(len);

                            //invio 0 per segnalare che la data richiesta non è presente (serve a far capire al peer che ha tutto)
                            printf("[DS][LOG]  Date terminate, PRES_REQ terminata\n\n");

                            do{
                                ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                    if(ret < 0){
                                    sleep(POLLING_TIME);
                                    perror("Errore nella sendto");
                            }
                            }while(ret < 0 );

                            //esco dall'if(PRES_REQ)
                            break;
                        }

                        printf("[DS][LOG]  Porte del giorno %s terminate\n",data);

                        //send porta = 0
                        strcpy(str,"0");

                        len = strlen(str)+1;
                        lmsg = htons(len);

                        //invio la lunghezza della porta
                        do{
                        ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                            }
                        }while(ret < 0 );

                        printf("[DS][LOG]  Lunghezza inviata \n");

                        //invio porta
                        do{
                        ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                            }
                        }while(ret < 0 );

                        printf("[DS][LOG]  porta = 0 inviata \n");

                        //attendo ack
                        do{
                            ret = recvfrom(sd, (void*)msg, ACK_LEN, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                if(ret < 0){
                                    printf("[DS][ERR]  Timeout scaduto!\n");
                                    sleep(POLLING_TIME);                                   
                                    }
                        } while (ret < 0);
                
                        printf("[DS][LOG]  ACK ricevuta, PRES_REQ terminata \n");
                        
                }
                //Se la richiesta è di DISC_REQ la gestisco
                if(strcmp(msg,"DISC_REQ") == 0){

                    //Variabili d'appoggio per i dati
                    char str[BUFFER_SIZE];
                    int porta,n1,n2,portaS;
                    printf("[DS][LOG]  Richiesta di disconnessione al network individuata\n");
                    porta = ntohs(connecting_addr.sin_port);

                    //ELIMINO PEER[PORTA] DALLA LISTA DEI CONNESSI
                    estraiPeer(porta);
                    //AGGIORNO I NEIGHBORS
                    aggiornaNeig();
                    
                    //Invio messaggio di ACK
                    strcpy(str,"ACK");
                    do{
                        ret = sendto(sd, (void*)&str, ACK_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                        if(ret < 0){
                            sleep(POLLING_TIME);
                            perror("Errore nella sendto");
                        }
                    }while(ret < 0 );
                    printf("[DS][LOG]  Peer[%d] disconnesso \n",porta);

                    //INVIO I NEIGHBORS A TUTTI GLI ALTRI
                    //Invio agli altri peer i neighbor aggiornati

                    printf("\n[DS][LOG]  Controllo eventuali aggiornamenti da fare\n");
                    while(trovaMod(&n1,&n2,&portaS) != 0){
                        char* agg = "AGG_NEIG\0";
                        len = strlen(agg);
                        
                        //Creo l'indirizzo di invio
                        memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                        //informazioni sul peer
                        connecting_addr.sin_family = AF_INET ;
                        connecting_addr.sin_port = htons(portaS);
                        inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);

                        //Invio msg di AGG_NEIGHBORS
                        printf("[DS][LOG]  Invio AGG_NEIG a [%d]\n",portaS);
                        do{
                            ret = sendto(sd, (void*)agg, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                perror("Errore nella sendto");
                                sleep(POLLING_TIME);
                            }
                        }while(ret < 0);
                        
                        //Invio nuovi valori

                        
                        printf("[DS][LOG]  Invio neighbors a [%d]\n",portaS);
                        sprintf(str,"%d %d",n1,n2);
                        len = strlen(str)+1;
                        do{
                            ret = sendto(sd, (const char*)str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                perror("Errore nella sendto");
                                sleep(POLLING_TIME);
                            }
                        }while(ret < 0);
                        
                        printf("[DS][LOG]  Aggiornamento effettuato con successo\n");
                    //TERMINE FASE DI AGGIORNAMENTO NEIGHBORS
                    }


                }
                //Se la richiesta è di BOOT_REQ la gestisco
                if(strcmp(msg,"BOOT_REQ") == 0){

                    //variabili d'appoggio per i dati
                    char str[BUFFER_SIZE];
                    int n1,n2,portaS;
                    struct peer* new = (struct peer*)malloc(sizeof(struct peer));
                    printf("[DS][LOG]  Richiesta di connessione al network individuata\n");
                    
                    //converto ip e porta
                    new->ip = inet_ntoa(connecting_addr.sin_addr);
                    new->porta = ntohs(connecting_addr.sin_port);
                    inserisciPeerOrd(new);
                    
                    sprintf(str,"ACK %d %d/0", new->neighbor1, new->neighbor2);
                    //determino la lunghezza del messaggio
                    len = strlen(str)+1;
                    lmsg = htons(len);
                    //Invio un lunghezza risposta
                    printf("[DS][LOG]  Invio lunghezza ACK [%d]\n",new->porta);
              
                    do{
                        ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                        if(ret < 0){
                            sleep(POLLING_TIME);
                            perror("Errore nella sendto");
                        }
                    }while(ret < 0 );

                    printf("[DS][LOG]  Lunghezza inviata \n");

                    //Invio ACK e neighbors
                    do{
                        ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                        if(ret < 0){
                            sleep(POLLING_TIME);
                            perror("Errore nella sendto");
                        }
                    }while(ret < 0 );
                    printf("[DS][LOG]  Neighbors inviati \n");

                    inserisciPresenza(new->porta);

                    printf("[DS][LOG]  Registro presenze agiornato\n");

                    //stampaPresenze();

                    printf("[DS][LOG]  Peer [%s][%d] connesso\n",new->ip,new->porta);
                    //TERMINE FASE DI BOOT

                    //Invio agli altri peer i neighbor aggiornati

                    printf("\n[DS][LOG]  Controllo eventuali aggiornamenti da fare\n");
                    while(trovaMod(&n1,&n2,&portaS) != 0){
                        char* agg = "AGG_NEIG\0";
                        len = strlen(agg);
                        
                        //Creo l'indirizzo di invio
                        memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                        //informazioni sul peer
                        connecting_addr.sin_family = AF_INET ;
                        connecting_addr.sin_port = htons(portaS);
                        inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);

                        //Invio msg di AGG_NEIGHBORS
                        printf("[DS][LOG]  Invio AGG_NEIG a [%d]\n",portaS);
                        do{
                            ret = sendto(sd, (void*)agg, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                perror("Errore nella sendto");
                                sleep(POLLING_TIME);
                            }
                        }while(ret < 0);
                        
                        //Invio nuovi valori

                        
                        printf("[DS][LOG]  Invio neighbors a [%d]\n",portaS);
                        sprintf(str,"%d %d/0",n1,n2);
                        len = strlen(str);
                        do{
                            ret = sendto(sd, (const char*)str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                perror("Errore nella sendto");
                                sleep(POLLING_TIME);
                            }
                        }while(ret < 0);
                        
                        printf("[DS][LOG]  Aggiornamento effettuato con successo\n");
                    //TERMINE FASE DI AGGIORNAMENTO NEIGHBORS
                    }
                    
                }
            }
            //Controllo eventuali input da stdin
            if(FD_ISSET(STDIN, &read_fds)){

                //pulizia delle variabili "stringa" poichè potrebbero essere state sporcate da un ciclo precedente
                strcpy(buffer,"");
                strcpy(comando,"");
                strcpy(opzione1,"");

                //Attesa stringa da stdin
                char* val = fgets(buffer,sizeof(buffer), stdin);
                //controllo di eventuali errori nella fgets() 
                if(val == NULL){
                    printf("[DS][ERR]  Comando non valido, inserire uno dei seguenti comandi:\n");
                    stampaComandi();
                    continue;
                }

                //scomposizione della stringa in 2 stringhe per poterle gestire separatamente
                sscanf(buffer,"%s %s", comando, opzione1);

                if(!strcmp(comando,"help")){
                    if((strlen(opzione1) == 0)){        

                        printf("\n[DS][LOG]  Comando %s accettato\n\n", comando);

                        help();

                    }else{

                        printf("[DS][ERR]  formato non corretto\n");
                        continue;
                    }
                }else if(!strcmp(comando,"showpeers")){
                    if((strlen(opzione1) == 0)){        

                        printf("\n[DS][LOG]  Comando %s accettato\n\n", comando );

                        showPeers();

                    }else{

                        printf("[DS][ERR]  formato non corretto\n");
                        continue;
                    }
                }else if(!strcmp(comando,"showneighbors")){
                    if((strlen(opzione1) != 0)){        

                        printf("\n[DS][LOG]  Comando %s %s accettato\n\n", comando, opzione1);

                        showNeighbors(atoi(opzione1));   

                    }else if((strlen(opzione1) == 0)){

                         printf("\n[DS][LOG]  Comando %s accettato\n\n", comando);

                         showNeighborsAll();

                    }else{

                        printf("[DS][ERR]  formato non corretto\n");
                        continue;
                    }
                }else if(!strcmp(comando,"esc")){
                    if((strlen(opzione1) == 0)){  

                        printf("\n[DS][LOG]  Comando %s accettato\n\n", comando);

                        char* cmd;  //Conterà il comando da inviare

                        struct peer* s = coda;  //Coda dei peer

                        //CICLO E SULLA CODA ED AD OGNI PEER INVIO UN COMANDO DI DISCONNESSIONE
                        while(s != NULL){

                                cmd = "DS__EXIT\0";

                                //pulizia
                                memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                               //informazioni sul peer
                                connecting_addr.sin_family = AF_INET ;
                                connecting_addr.sin_port = htons(s->porta);
                                inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);

                                printf("[DS][LOG]  Tentativo di invio di DS__EXIT...\n");
                                
                                //Invio del DS__EXIT al peer
                                do
                                {
                                    ret = sendto(sd, cmd, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                    if(ret < 0){
                                        perror("Errore nell'invio del DS__EXIT");
                                        sleep(POLLING_TIME);
                                    }
                                } while (ret < 0);
                                printf("[DS][LOG]  DS__EXIT inviata con successo!\n");
                                printf("[DS][LOG]  Attesa di risposta dal peer...!\n");
                                
                                //Attesa del ACK dal peer
                                do
                                {
                                    ret = recvfrom(sd, (void*)msg, ACK_LEN, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                    if(ret < 0){
                                        printf("[DS][ERR]  Timeout scaduto, discconnessione fallita!\n");
                                    
                                    }
                                } while (ret < 0);

                                //printf("%s\n",msg);
                                if(strcmp(msg,"ACK") == 0){
                                   printf("[DS][LOG]  Peer:[%d] disconnesso\n",s->porta);
                                }
                                s = s->next;
                        }
                        printf("[DS][LOG]  DS disconnesso con successo!\n");
                        return 0;

                    }else{
                        printf("[DS][ERR]  formato non corretto\n");
                        continue;
                    }
                }else{

                        printf("[DS][ERR]  non è stato inserito un comando valido!\n");

                        stampaComandi();
                        continue;
                }
                
            }//Fine if sul stdin pronto in read fds

        }//fine while(select)
       
        //Reinserisco sd e stdin nel set di lettura per eventuali richieste multiple
        FD_SET(sd, &read_fds);
        FD_SET(STDIN, &read_fds);

        //CONTROLLO CHE SIANO LE 18 PER LA CHIUSURA DEL REGISTRO GIORNALIERO
        getHour(&ora);
        if(ora == 18 && chiuso == 0){
            aggiornaPresenze();
            chiuso = 1;
            printf("[DS][LOG]  Aggiornamento registro delle presenze!\n");
        }else if( ora != 18 && chiuso == 1){
            chiuso = 0;
        }
    }//fine while generale
    
    printf("[DS][LOG]   Socket %d chiuso\n", sd);
    close(sd);


    return 0; 
}