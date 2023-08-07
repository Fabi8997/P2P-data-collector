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

#define BUFFER_SIZE 1024
#define COMMAND_SIZE 25
#define POLLING_TIME 3
#define STDIN 0
#define NEIGH_LEN 10 //ACK %d %d\0
#define ACK_LEN 4 //ACK\0
#define REQ_LEN 9 // BOOT_REQ\0 DISC_REQ\0 ...
#define N_LEN 20
#define DATE_LEN 12 // dd:mm:yyyy 
//indica se il peer è connesso al server
int connesso = 0;
//buffer da utilizzare per memorizzare la stringa dei comandi da stdin
char buffer[BUFFER_SIZE];
//comandi con relative ed eventuali opzioni
char comando[COMMAND_SIZE], opzione1[COMMAND_SIZE], opzione2[COMMAND_SIZE], opzione3[COMMAND_SIZE], opzione4[COMMAND_SIZE];
//porta del peer 
int portaPeer;
//porta del DS
int portaDS;
//neighbors
int neighbor1;
int neighbor2;

//variabile per gestire la chiusura del registro giornaliero
int regChiuso = 0;

//ora corrente
int ora;

//STRUTTURA DATI CHE RAPPRESENTA UNA ENTRY
struct entry{
   
    //DATA NELLA QUALE È STATA AGGIUNTA LA ENTRY  
    char date[12];
    
    //TIPO DELLA ENTRY:  t = nuovo tampone;  p = nuovo caso;
    char type[4];

    //NUMERO DI PORTA IN CUI È STATA REGISTRATA LA ENTRY,
    // SERVE PER EVITARE DOPPIONI NEL REG GENERALE
    int porta;

    //NUMERO DI DATI ASSOCIATI ALLA ENTRY
    int quantity;

    //PUNTATORE PER REALIZZARE LE LISTE DEI REGISTRI
    struct entry* next;
};

//STRUTTURA DATI CHE RAPPRESENTA UN RISULTATO DI UNA AGGREGAZIOONE
struct aggr{

    //COMANDO DI AGGREGAZIONE SALVATO
    char aggr[45];

    //VALORE DEL RISULTATO DELLA AGGREGAZIONE
    int valore;

    //PUNTATORE PER REALIZZARE UNA LISTA DELLE AGGREGAZIONI SALVATE
    struct aggr* next;
};

//REGISTRO IN CUI VENGONO SALVATE TUTTE LE ENTRY GIORNALIERE
struct entry* RegGiornaliero = NULL;

//REGISTRO IN CUI VENGONO SALVATE TUTTE LE ENTRY DEI GIORNI PRECEDENTI
struct entry* RegGenerale = NULL;

//REGISTRO IN CUI VENGONO SALVATE TUTTE LE AGGREGAZIONI RICHIESTE IN PRECEDENZA
struct aggr* RegAggr = NULL;

//STAMPA I COMANDI POSSIBILI
void stampaComandi(){

    char* comandi = "\n***************************** PEER COVID STARTED *******************************\n"
            "Digita un comando:\n"
            "1) start <DS_addr> <DS_port> --> richiede la connessione al network\n"
            "2) add <type> <quantity> --> aggiunge l'evento al register odierno \n"
            "3) get <aggr> <type> <period> --> richiede l'elaborazione del dato aggregato\n"
            "4) stop --> richiesta di disconnessione al network\n\n";

    printf("%s", comandi);
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

//RICAVA LA DATA ODIERNA
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

//RESTITUISCE LA DATA ODIERNA
void getDateOdierna(char* str){        

    time_t rawtime;   
    char dd[3];
    char mm[3];
    char yyyy[5];
    time(&rawtime);
    struct tm *info;
    info = localtime( &rawtime );
    
    if(info->tm_mon+1 < 10){
        sprintf(mm,"0%d",info->tm_mon+1);
    }else{
        sprintf(mm,"%d",info->tm_mon);
    }
    if(info->tm_mday+1 < 10){
        sprintf(dd,"0%d",info->tm_mday+1);
    }else{
        sprintf(dd,"%d",info->tm_mday);
    }
    sprintf(yyyy,"%d",info->tm_year+1900);
    
    sprintf(str,"%s:%s:%s",dd,mm,yyyy);

}

//INSERISCE UNA NUOVA ENTRY AL REGISTRO GIORNALIERO, VENGONO PASSATI IL TIPO E IL NUMERO DI NUOVI DATI
void inserisciEntry(char* tipo, int num, int porta){  
 
    struct entry* new = (struct entry*)malloc(sizeof(struct entry));


    new->quantity = num;
    new->porta = porta;
    strcpy(new->type, tipo);

    getDate(new->date);
    //printf("%s\n",new->date);

    //printf("%s %s %d\n",new->date,new->type,new->quantity);
    
    
    new->next = RegGiornaliero;
    RegGiornaliero = new;
    printf("[PEER][LOG]  Entry: %s %s %d %d inserita correttamente\n", new->date, new->type, new->quantity, new->porta);
    return;
}

//INSERISCE IN REGISTRO GENERALE, VIENE PASSATO UN PUNTATORE ALL'ELEMENTO DA TRASFERIRE, È UTILIZZATA DA chiusuraRegGiornaliero()
void inserisciRegGenerale(struct entry* elemento){

    //struct entry* work;

    if(RegGenerale == NULL){
        elemento->next = NULL;
        RegGenerale = elemento;
        //printf("Primo elemento della lista inserito!\n");
        return;
    }
   
    elemento->next = RegGenerale;
    RegGenerale = elemento;
    
    return;
}

//CHIUDE IL REGIRSTRO GIORNALIERO E INSERISCE LE SUE ENTRY NEL REGISTRO GENERALE IN FORMA RIASSUNTA
void chiusuraRegGornaliero(){  //DONE

    struct entry* s;
    //struct entry* work;
    struct entry* nuovicasi = (struct entry*)malloc(sizeof(struct entry));
    struct entry* tamponi = (struct entry*)malloc(sizeof(struct entry));

    //INIZIALIZZO NUOVICASI E TAMPONI ODIERNI

    getDateOdierna(nuovicasi->date);
    getDateOdierna(tamponi->date);
    nuovicasi->quantity = 0;
    tamponi->quantity = 0;
    nuovicasi->porta = portaPeer;
    tamponi->porta = portaPeer;
    strcpy(nuovicasi->type,"n");
    strcpy(tamponi->type,"t");
        
    s = RegGiornaliero;

    while(s != NULL){
  
        if(strcmp(s->type,"n") == 0){
            nuovicasi->quantity += s->quantity;
        }else if(strcmp(s->type,"t") == 0){
            tamponi->quantity += s->quantity;
        }
        s = s->next;
    }
    RegGiornaliero = NULL;

    inserisciRegGenerale(nuovicasi);
    inserisciRegGenerale(tamponi);
    return;
}

//STAMPA IL REGISTRO GENEREALE
void stampaRegGen(){        // PER DEBUG DEL REG GENERALE

struct entry* s ;
    s = RegGenerale;
    if(s == NULL){
        printf("Reg generale vuoto\n");
        return;
    }else{
        printf("Reg Generale: \n");
    }
    while(s != NULL){
        printf("%s %s %d %d\n",s->date, s->type, s->quantity, s->porta);
        s = s->next;
    }
    
}

//STAMPA IL REGISTRO GIORNALIERO
void stampaRegGiornaliero(){    //PER DEBUG DEL REG GIORNALIERO

    struct entry* s ;
    s = RegGiornaliero;
    if(s == NULL){
        printf("Reg giornaliero vuoto\n");
        return;
    }else{
        printf("Reg giornaliero: \n");
    }
    while(s != NULL){
        printf("%s %s %d\n",s->date, s->type, s->quantity);
        s = s->next;
    }
}

//CONTROLLA SE LA ENTRI CON DATA TIPO E PORTA È PRESENTE NEL REG GENERALE, SE C'È RESTITUISCE 1, ALTRIMENTI 0
int entryTrovata(char* data, char* tipo, int porta){
    //Variabile per la scansione
    struct entry* s;

    s = RegGenerale;


    while(s != NULL){
        if((strcmp(s->date,data) == 0) && (strcmp(s->type,tipo) == 0) && s->porta == porta){
            //printf("Entry trovata!\n");
            return s->quantity;
        }
        s = s->next;
    }
    return -1;
}

//INSERISCE L'AGGREGAZIONE str CON RISULTATO num NEL REGISTRO DELLE AGGREGAZIONI
void inserisciAggr(char* str, int num){

    //Struttura da inserire nella lista
    struct aggr* new = (struct aggr*)malloc(sizeof(struct aggr));

    new->valore = num;
    strcpy(new->aggr,str);

    //inserimento in testa
    new->next = RegAggr;
    RegAggr = new;

}

//STAMPA IL REGISTRO DELLE AGGREGAZIONI
void stampaAggr(){      //PER DEBUG DEL REG AGGR

    struct aggr* s;

    if(RegAggr == NULL){
        printf("vuoto\n");
        return;
    }

    s = RegAggr;
    printf("RegAggr:\n");
    while(s != NULL){
        printf("%s\n",s->aggr);

        s = s->next;
    }
}

//CERCA NELL REGISTRO DELLE AGGREGAZIONI str, RESTITUISCE 0 SE NON LA TROVA, IL VALORE DEL RISULTATO ALTRIMENTI
int trovaAggr(char* str){

    //per scorrere
    struct aggr* s;

    //Controlla se il reg è vuoto, ridondante con il primo controllo seguente nel while
    if(RegAggr == NULL){
        return -1;
    }

    s = RegAggr;

    while(s != NULL){
        if(strcmp(str,s->aggr) == 0){

            //Ritorna il valore della aggregazione
            return s->valore;
        }

        s = s->next;
    }

    return -1;

}

//Prende in ingresso una data in formato dd:mm:yyyy e la decrementa, sovrascrivendola a data
void decrementaData( char* data){

    //Variabili per splittare con strtok
    char* token = NULL;
    const char delimiter[] = ":";

    //variabili d'appoggio per i calcoli
    char giorno[3];
    char mese[3];
    char anno[5];
    int g,m,a;

    token = strtok(data,delimiter);
    strcpy(giorno,token); 
    token = strtok(NULL, delimiter);                                     
    strcpy(mese,token);
    token = strtok(NULL, delimiter);                                     
    strcpy(anno,token);

    g = atoi(giorno);
    m = atoi(mese);
    a = atoi(anno);

    if(g == 1){
        if(m == 1 || m == 2 || m == 4 || m == 6 || m == 8 || m == 9 || m == 12 ){
            g = 31;
        }else if(m == 3){
            g = 28;
        }else{
            g = 30;
        }

        if(m == 1){
            m = 12;
            a--;
        }else{
            m--;
        }
    }else{
        g--;
    }

    if(m < 10){
        sprintf(mese,"0%d",m);
    }else{
        sprintf(mese,"%d",m);
    }
    if(g < 10){
        sprintf(giorno,"0%d",g);
    }else{
        sprintf(giorno,"%d",g);
    }
    sprintf(anno,"%d",a);
    
    sprintf(data,"%s:%s:%s",giorno,mese,anno);
    
}

//Controllo se la data passata è presente nel Reg Generale, serve per il caso di data1 = *, 1 se lo trova, 0 altrimenti
int dataTrovata(char* data){


    //Variabile per scorrere il registro generale
    struct entry* s = RegGenerale;

    while(s != NULL){
        if(strcmp(s->date,data) == 0)
            return 1;

            s = s->next;
    }

    return 0;
}

//Prende in ingresso aggr type data1 data2 e calcola l'aggr richiesto, restituisce -1 se le date non sono consistenti
int calcoloTotale(char* operazione, char* tipo){

    //Variabili d'appoggio per aggr tipo ed eventuale tipo2 in caso di tipo = n (nuovo caso)
    char aggr[11];  //totale o variazione

    char tipo1[8];  //tampone o nuovo

    char tipo2[5];  //caso

    //Data più remota
    char data1[12];

    //Data più recente
    char data2[12];

    //Risultato dell'operazione, se rimane a -1 significa che 
    int risultato = -1;

    //Estraggo le informazioni per l'operazione
    //printf("controllo tipo\n");
    if(strcmp(tipo,"t") == 0){
        
        sscanf(operazione,"%s %s %s %s",aggr, tipo1, data1, data2);

    }else{

        sscanf(operazione,"%s %s %s %s %s",aggr, tipo1, tipo2, data1, data2);

    }

    if(strcmp(data2,"*") == 0){

        getDate(data2);
        decrementaData(data2);

        //printf("%s\n",data2);
    }

    while( strcmp(data1,data2) != 0 && dataTrovata(data2) == 1){
        //printf("dentro while\n");

        struct entry* s;
            
        if( RegGenerale == NULL)
                return -1;

        s = RegGenerale;

        while (s != NULL){
            
            //printf("controllo date\n");
            if(strcmp(s->date,data2) == 0 && strcmp(tipo, s->type) == 0){
                if(risultato == -1){
                    //printf("aggiorno res\n");
                    risultato = s->quantity;
                }else{
                    //printf("aggiorno res\n");
                    risultato += s->quantity;
                }
            }

            s = s->next;
        }       
        
        //printf("sono fuori\n");
    
        decrementaData(data2); 
    }

    //SONO ARRIVATO ALL'ULTIMO GIORNO
        struct entry* s;
            
        if( RegGenerale == NULL)
                return -1;

        s = RegGenerale;

        while (s != NULL){
            
            if(strcmp(s->date,data2) == 0 && strcmp(tipo, s->type) == 0){
                if(risultato == -1){
                    risultato = s->quantity;
                }else{
                    risultato += s->quantity;
                }
            }

            s = s->next;
        }

    return risultato;
}

//Prende in ingresso due date su un tipo e ne calcola la variazione, data1 è la meno recente, data2 la più recente
int calcoloVariazione(char* data1, char* data2, char* tipo){

    //Per scorrere la lista
    struct entry* s = RegGenerale;
    
    //totale giornaliero di data1
    int valore1 = 0;

    //totale giornaliero di data2
    int valore2 = 0;

    //risultato
    int risultato = 0;

    while(s != NULL){

        if(strcmp(data1,s->date) == 0 && strcmp(tipo,s->type) == 0){
            valore1 += s->quantity;
        }

        s = s->next;
    }

    s = RegGenerale;

    while(s != NULL){

        if(strcmp(data2,s->date) == 0 && strcmp(tipo,s->type) == 0){
            valore2 += s->quantity;
        }

        s = s->next;
    }

    risultato = valore2 - valore1;

    return risultato;
}

//Carica il proprio registro generale dal file RegistroEntry.txt
void caricaRegGenerale(){

    FILE *fptr;

    //Conterrà la data letta da file
    char data[12];

    //Conterrà il tipo letto da file
    char tipo[2];

    //conterrà il valore della entry
    int quanti;

    //porta in cui è stata registrata la entry
    int porta;  

    //Variabile d'appoggio
    char e[60];

    struct entry* nuova;

    if(portaPeer == 5001){
        fptr = fopen("5001.txt","r");
    }else if( portaPeer == 5002){
        fptr = fopen("5002.txt","r");
    }else if( portaPeer == 5003){
        fptr = fopen("5003.txt","r");
    }else if( portaPeer == 5004){
        fptr = fopen("5004.txt","r");
    }else if( portaPeer == 5005){
        fptr = fopen("5005.txt","r");
    }else{
        return;
    }

    

    if(fptr == NULL){
        perror("errore");
        return;
    }

    while(fscanf(fptr,"%s %s %d %d", data, tipo, &quanti, &porta) != EOF ){
    
        sprintf(e,"%s %s %d %d",data,tipo,quanti,porta);
        nuova = (struct entry*)malloc(sizeof(struct entry));
        nuova->porta = porta;
        strcpy(nuova->date,data);
        strcpy(nuova->type,tipo);
        nuova->quantity = quanti;

        inserisciRegGenerale(nuova);
        
    }

    fclose(fptr);

}

//Salva il proprio registro generale nel file RegistroEntry.txt
void salvaRegGenerale(){

    char e[60];
    FILE *fptr;
    struct entry* s = RegGenerale;

    
    if(portaPeer == 5001){
        fptr = fopen("5001.txt","w");
    }else if( portaPeer == 5002){
        fptr = fopen("5002.txt","w");
    }else if( portaPeer == 5003){
        fptr = fopen("5003.txt","w");
    }else if( portaPeer == 5004){
        fptr = fopen("5004.txt","w");
    }else if( portaPeer == 5005){
        fptr = fopen("5005.txt","w");
    }else{
        return;
    }

        if(fptr == NULL){
            perror("errore");
            return;
        }


        while(s != NULL){
            //printf("inserisco\n");

            sprintf(e,"%s %s %d %d\n",s->date, s->type, s->quantity, s->porta );
            fprintf(fptr,"%s",e);

            s = s->next;
        }    
    

    fclose(fptr);

}


int main(int argc, char** argv){

    //variabili di appoggio
    int ret, sd, maxfd, size;
    socklen_t len;
    socklen_t addrlen;
    u_int16_t lmsg;
    struct sockaddr_in my_addr,srv_addr,connecting_addr; 
    
    //set di fd da monitorare 
    fd_set read_fds;

    //variabili di appoggio
    char str1[BUFFER_SIZE];

    //variabile per far sì che la select non sia bloccante
    struct timeval timeout = {0,0};

    //variabile per indicare il timeout entro il quale occorre reinviare la BOOT_REQ al server
    struct timeval socktv = {10,0};
    addrlen = sizeof(connecting_addr);

    char msg[BUFFER_SIZE];

    //controllo se il è presente il numero di porta
    if(argc == 2){
        portaPeer = atoi(argv[1]);
        printf("[PEER][LOG]  Porta peer: %d\n", portaPeer);
    }else{
        perror("[PEER][ERR]  Non è stata inserita la porta\n");
        exit(-1);
    }

    caricaRegGenerale();

    //stampaRegGen();

    //Creazione socket UDP
    sd = socket(AF_INET,SOCK_DGRAM, 0);
    if(sd < 0){
        perror("[PEER][ERR]  Errore in fase di creazione del socket: \n" );
        exit(-1);
    }else{
        printf("[PEER][LOG]  Socket %d creato correttamente\n",sd);
    }

   
    if (setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO,&socktv,sizeof(socktv))<0) 
        perror("Errore");


    //Creazione indirizzo
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET ;
    my_addr.sin_port = htons(portaPeer);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    ret = bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr));
    if(ret < 0){
        perror("[PEER][ERR]  Errore in fase di bind: \n" );
        exit(-1);
    }else{
        printf("[PEER][LOG]  Bind completata correttamente\n");
    }

    //pulizia
    FD_ZERO(&read_fds);
    
    maxfd = (sd > STDIN)?sd:STDIN;
    maxfd++;

    //stampa i possibili comandi
    stampaComandi();

    //imposto il set di lettura
    FD_SET(sd, &read_fds);
    FD_SET(STDIN, &read_fds);

    while(1){
            //estraggo i descrittori pronti dal read_fds set
        while(select(maxfd, &read_fds, NULL, NULL, &timeout) > 0){
            if(FD_ISSET(sd, &read_fds)){
                //controlla eventuali messaggi in entrata
                
                //printf("TROVATO sd pronto\n");  //DEBUG
                
                //Leggo la richiesta su sd
                do{              
                    ret = recvfrom(sd, (void*)&msg, REQ_LEN, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                    //printf("ret: %d\n",ret);  //DEBUG
                    //printf("%s\n",msg);       //DEBUG
                    if(ret < 0){
                        perror("Errore nella ricezione");
                        sleep(POLLING_TIME);
                    
                    }
                }while(ret < 0);

                if(strcmp(msg,"FLOOD_RQ") == 0){

                    //Conterrà i dati della entry richiesta
                    char str[45];

                    //Data della entry richiesta
                    char data[12];

                    //Tipo della entry richiesta
                    char tipo[2];

                    //Porta del neighbor che ha chiesto il flood
                    int portaN;

                    //Porta del Requester;
                    int portaR;

                    //Valore da restiture -1 se nessuno ha la entry, altrimenti il valore
                    int valore = -1;

                    printf("\n[PEER][LOG]  Ricevuta una richiesta di FLOOD_FOR_ENTRIES\n");

                    //Attendo la lunghezza del messaggio
                    do
                    {
                        ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&connecting_addr, &addrlen);
                        if(ret < 0){
                            printf("[PEER][ERR]  lunghezza non ricevuta!\n");        
                        }
                    } while (ret < 0);

                    size = ntohs(lmsg);
                    //printf("SIZE RICEVUTA %d\n", size); //DEBUG

                    //attendo il messaggio
                    do
                    {
                        ret = recvfrom(sd, (void*)&str,size, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                        if(ret < 0){
                            printf("[PEER][ERR]  messaggio di flood non ricevuto!\n");        
                        }
                    } while (ret < 0);

                    sscanf(str,"%s %s %d %d",data, tipo, &portaR, &portaN);

                    printf("[PEER][LOG]  Ricevuta richiesta per la entry %s %s %d\n",data,tipo,portaR);

                    ret = entryTrovata(data,tipo,portaR);

                    if(ret >= 0){
                        
                        printf("[PEER][LOG]  Entry trovata\n");

                        sprintf(str,"%d",ret);
                        
                        len = strlen(str) + 1;
                        lmsg = htons(len);

                        //invio la lunghezza del valore
                        do{
                            ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                            }
                        }while(ret < 0 );

                        //invio il valore
                        do{
                            ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                            }
                        }while(ret < 0 );

                        printf("[PEER][LOG]  Valore inviato: %s\n" ,str);
                        printf("[PEER][LOG]  FLOOD_FOR_ENTRIES terminata\n");
                        continue; //******
                    }
                    //ELSE?
                    printf("[PEER][LOG]  Entry non trovata inoltro la richiesta di flooding\n");

                    //Invio richiesta a n1 se fa
                    if((neighbor1 != 0) && (neighbor1 <= portaPeer) && (neighbor1 != portaN)){
                        
                        char* cmd = "FLOOD_RQ\0";

                        printf("[PEER][LOG]  Invio richiesta di FLOOD_FOR_ENTRIES al neighbor1 %d\n",neighbor1);

                         //pulizia
                        memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                        //informazioni sul peer
                        connecting_addr.sin_family = AF_INET ;
                        connecting_addr.sin_port = htons(neighbor1);
                        inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);

                        do
                        {
                            ret = sendto(sd, (void*)cmd, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                perror("Errore nell'invio del FLOOD_RQ");
                                sleep(POLLING_TIME);
                            }
                        } while (ret < 0);

                        //printf("[PEER][LOG]  Invio lunghezza \n");
                                    
                        sprintf(str,"%s %s %d %d", data,tipo,portaR,portaPeer);            

                        //determino la lunghezza del messaggio
                        len = strlen(str)+1;
                        //printf("%d\n",len);
                        lmsg = htons(len);

                        //Invio lunghezza entry
                        do{
                            ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                                }
                        }while(ret < 0 );

                        printf("[PEER][LOG]  Lunghezza inviata \n");

                        //Invio messaggio contenente la entry
                        do{
                            ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                            }
                        }while(ret < 0 );

                        printf("[PEER][LOG]  Richiesta %s inviata \n", str);

                        printf("[PEER][LOG]  Lunghezza ricevuta\n");

                        //Attendo la lunghezza del messaggio
                        do
                        {
                            ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&connecting_addr, &addrlen);
                            if(ret < 0){
                                printf("[PEER][ERR]  lunghezza non ricevuta!\n");        
                        }
                        } while (ret < 0);

                        size = ntohs(lmsg);
                        //printf("SIZE RICEVUTA %d\n", size); //DEBUG

                        //attendo il messaggio
                        do
                        {
                            ret = recvfrom(sd, (void*)&str,size, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                            if(ret < 0){
                                printf("[PEER][ERR]  messaggio di flood non ricevuto!\n");        
                            }
                        } while (ret < 0);

                        printf("[PEER][LOG]  Valore ricevuto: %s\n",str);

                        sscanf(str,"%d",&valore);

                        
                    }
                    
                    sprintf(str,"%s %s %d %d", data,tipo,portaR,portaPeer);

                    //Invio richiesta a n2 se fa
                    if((neighbor2 != 0) && (neighbor2 >= portaPeer) && (neighbor2 != portaN)){

                        char* cmd = "FLOOD_RQ\0";

                        printf("[PEER][LOG]  Invio richiesta di FLOOD_FOR_ENTRIES al neighbor2 %d\n",neighbor2);

                         //pulizia
                        memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                        //informazioni sul peer
                        connecting_addr.sin_family = AF_INET ;
                        connecting_addr.sin_port = htons(neighbor2);
                        inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);

                        do
                        {
                            ret = sendto(sd, (void*)cmd, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                perror("Errore nell'invio del FLOOD_RQ");
                                sleep(POLLING_TIME);
                            }
                        } while (ret < 0);

                        //printf("[PEER][LOG]  Invio lunghezza \n");
                                    
                        sprintf(str,"%s %s %d %d", data,tipo,portaR,portaPeer);            

                        //determino la lunghezza del messaggio
                        len = strlen(str)+1;
                        //printf("%d\n",len);
                        lmsg = htons(len);

                        //Invio lunghezza entry
                        do{
                            ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                                }
                        }while(ret < 0 );

                        printf("[PEER][LOG]  Lunghezza inviata \n");

                        //Invio messaggio contenente la entry
                        do{
                            ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                            }
                        }while(ret < 0 );

                        printf("[PEER][LOG]  Richiesta %s inviata \n", str);

                        printf("[PEER][LOG]  Lunghezza ricevuta\n");

                        //Attendo la lunghezza del messaggio
                        do
                        {
                            ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&connecting_addr, &addrlen);
                            if(ret < 0){
                                printf("[PEER][ERR]  lunghezza non ricevuta!\n");        
                        }
                        } while (ret < 0);

                        size = ntohs(lmsg);
                        //printf("SIZE RICEVUTA %d\n", size); //DEBUG

                        //attendo il messaggio
                        do
                        {
                            ret = recvfrom(sd, (void*)&str,size, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                            if(ret < 0){
                                printf("[PEER][ERR]  messaggio di flood non ricevuto!\n");        
                            }
                        } while (ret < 0);

                        printf("[PEER][LOG]  Valore ricevuto: %s\n",str);

                        sscanf(str,"%d",&valore);
                        
                    }

                    sprintf(str,"%d",valore);
                    //inoltro la risposta

                    len = strlen(str) + 1;
                    lmsg = htons(len);

                    //pulizia
                    memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                    //informazioni sul peer
                    connecting_addr.sin_family = AF_INET ;
                    connecting_addr.sin_port = htons(portaN);
                    inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);

                    //invio la lunghezza del valore
                    do{
                        ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                        if(ret < 0){
                            sleep(POLLING_TIME);
                            perror("Errore nella sendto");
                        }
                    }while(ret < 0 );

                    //invio il valore
                    do{
                        ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                        if(ret < 0){
                            sleep(POLLING_TIME);
                            perror("Errore nella sendto");
                        }
                    }while(ret < 0 );

                    printf("[PEER][LOG]  Valore inviato: %s\n",str);

                    printf("[PEER][LOG]  FLOOD_FOR_ENTRIES terminata\n");
                }

                if(strcmp(msg,"REQ_DATA") == 0){

                    //Conterrà il dato aggregato richiesto
                    char str[45];

                    printf("\n[PEER][LOG]  Ricevuta una richiesta di REQ_DATA\n");

                    //Attendo la lunghezza del messaggio
                    do
                    {
                        ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&connecting_addr, &addrlen);
                        if(ret < 0){
                            printf("[PEER][ERR]  lunghezza non ricevuta!\n");        
                        }
                    } while (ret < 0);

                    size = ntohs(lmsg);
                    //printf("SIZE RICEVUTA %d\n", size); //DEBUG

                    //attendo il messaggio
                    do
                    {
                        ret = recvfrom(sd, (void*)&str,size, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                        if(ret < 0){
                            printf("[PEER][ERR]  entry non ricevuta!\n");        
                        }
                    } while (ret < 0);
                    printf("[PEER][LOG]  Ricevuta richiesta per: %s\n",str);

                    ret = trovaAggr(str);
                    if(ret < 0){

                        printf("[PEER][LOG]  Dato aggregato non trovato");

                        len = 0;
                        lmsg = htons(len);

                        //invio 0 per segnalare che la aggr richiesta non è presente
                        do{
                            ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                            }
                        }while(ret < 0 );

                    }else{

                        printf("[PEER][LOG]  Dato aggregato trovato\n");

                        sprintf(str,"%d",ret);
                        
                        len = strlen(str) + 1;
                        lmsg = htons(len);

                        //invio la lunghezza del valore
                        do{
                            ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                            }
                        }while(ret < 0 );

                        //invio il valore
                        do{
                            ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                            if(ret < 0){
                                sleep(POLLING_TIME);
                                perror("Errore nella sendto");
                            }
                        }while(ret < 0 );

                        printf("[PEER][LOG]  Valore inviato\n");
                    }

                }

                if(strcmp(msg,"AD_ENTRY")==0){

                    printf("\n[PEER][LOG]  Ricevuta una richiesta di AD_ENTRY\n");
                    //contiene  la entry
                    char ent[50];
                
                    //entry da inserire
                    struct entry* elemento = (struct entry*)malloc(sizeof(struct entry));
                    elemento->next = NULL;
                    do
                    {
                        ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&connecting_addr, &addrlen);
                        if(ret < 0){
                            printf("[PEER][ERR]  lunghezza non ricevuta!\n");        
                        }
                    } while (ret < 0);

                    size = ntohs(lmsg);
                    //printf("SIZE RICEVUTA %d\n", size); //DEBUG

                    //attendo il messaggio
                    do
                    {
                        ret = recvfrom(sd, (void*)&ent,size, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                        if(ret < 0){
                            printf("[PEER][ERR]  entry non ricevuta!\n");        
                        }
                    } while (ret < 0);
                    printf("[PEER][LOG]  Ricevuta la seguente entry: %s\n",ent);

                    //INSERIMENTO IN REG GENERALE
                    sscanf(ent,"%s %s %d %d",elemento->date,elemento->type,&elemento->quantity,&elemento->porta);


                    if(entryTrovata(elemento->date,elemento->type,elemento->porta) < 0){
                        inserisciRegGenerale(elemento);
                        printf("[PEER][LOG]  Entry inserita\n");
                    }else{
                        printf("[PEER][LOG]  Entry già presente\n");
                    }

                    //stampaRegGen();                    

                    strcpy(msg,"ACK");
                    do{
                        ret = sendto(sd, (void*)&msg, ACK_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                        if(ret < 0){
                            sleep(POLLING_TIME);
                            perror("Errore nella sendto");
                        }
                    }while(ret < 0 );

                    printf("[PEER][LOG]  ACK inviato correttamente al DS\n");
                }

        

                if(strcmp(msg,"DS__EXIT")==0){
                    char str[4];

                    printf("\n[PEER][LOG]  discovery server disconnesso, disconnessione in corso\n");
                    //Invio messaggio di ACK
                    strcpy(str,"ACK");
                    do{
                        ret = sendto(sd, (void*)&str, ACK_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                        if(ret < 0){
                            sleep(POLLING_TIME);
                            perror("Errore nella sendto");
                        }
                    }while(ret < 0 );
                    
                    printf("[PEER][LOG]  ACK inviato correttamente al DS\n");
                    printf("[PEER][LOG]  Salvataggio registro generale\n");
                    salvaRegGenerale();
                    printf("[PEER][LOG]  Peer chiuso, arrivederci");                  
                    
                    return 0;
                }

                //Controllo se il messaggio è di aggiornamento neighbors
                if(strcmp(msg,"AGG_NEIG") == 0){
                    char str2[BUFFER_SIZE];
                    printf("\n[PEER][LOG]  Ricevuta richiesta di aggiornamento neighbors\n");
                    
                    //Attendo i nuovi neighbors
                    do
                    {
                        ret = recvfrom(sd, str2, N_LEN, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                        if(ret < 0)
                            sleep(POLLING_TIME);
                    } while (ret < 0);
                    
                    //salvo i neighbors
                    sscanf(str2,"%d %d",&neighbor1,&neighbor2);
                    printf("[PEER][LOG]  Neighbors:  [%d]  [%d]\n",neighbor1,neighbor2);
                }
            }
            if(FD_ISSET(STDIN, &read_fds)){

                //Svuotamento delle variabili dei comandi
                strcpy(buffer, "");
                strcpy(comando, "");
                strcpy(opzione1, "");
                strcpy(opzione2, "");
                strcpy(opzione3, "");
                strcpy(opzione4,"");

                //Attesa stringa da stdin
                char* val = fgets(buffer,sizeof(buffer), stdin);

                printf("\n");

                //controllo di eventuali errori nella fgets() 
                if(val == NULL){
                    printf("[PEER][ERR]  Comando non valido, inserire uno dei seguenti comandi:\n");
                    stampaComandi();
                    continue;
                }
                    //scomposizione della stringa in 4 stringhe per poterle gestire separatamente
                    sscanf(buffer,"%s %s %s %s %s", comando, opzione1, opzione2, opzione3, opzione4);
                    //[DEBUG]
                    //printf("\ncomando digitato: %s %s %s %s\n",comando, opzione1, opzione2, opzione3);

                    if(!strcmp(comando,"start")){
                        if(connesso){
                             printf("[PEER][ERR]  Comando non valido, il peer è già connesso al DS\n");
                             continue;
                        }else{
                            if((strlen(opzione1) != 0) && (strlen(opzione2) != 0) && (strlen(opzione3) == 0)){
                                printf("[PEER][LOG]  Comando %s %s %s accettato\n", comando, opzione1, opzione2);
                                char* cmd;
                                //imposto il messaggio di richiesta di boot
                                cmd = "BOOT_REQ\0";

                                portaDS = atoi(opzione2);
                                //pulizia
                                memset(&srv_addr, 0, sizeof(srv_addr)); 
                               //informazioni sul server
                                srv_addr.sin_family = AF_INET ;
                                srv_addr.sin_port = htons(portaDS);
                                inet_pton(AF_INET, opzione1, &srv_addr.sin_addr);

                                riprova:
                                printf("[PEER][LOG]  Tentativo di invio di BOOT_REQ...\n");
                                
                                //Invio del BOOT_REQ al DS
                                do
                                {
                                    ret = sendto(sd, cmd, REQ_LEN, 0,(struct sockaddr*)&srv_addr, sizeof(srv_addr));
                                    if(ret < 0){
                                        perror("Errore nell'invio del BOOT_REQ");
                                        sleep(POLLING_TIME);
                                    }
                                } while (ret < 0);
                                printf("[PEER][LOG]  BOOT_REQ inviata con successo!\n");
                                printf("[PEER][LOG]  Attesa di risposta dal DS...!\n");
                                
                                //Attesa del BOOT_ACK dal DS
                                do
                                {
                                    ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&srv_addr, &len);
                                    if(ret < 0){
                                        printf("[PEER][ERR]  Timeout scaduto, BOOT fallito!\n");
                                        goto riprova;
                                    }
                                } while (ret < 0);

                                size = ntohs(lmsg);
                                //printf("SIZE RICEVUTA %d\n", size); //DEBUG

                                do
                                {
                                    ret = recvfrom(sd, (void*)&msg,size, 0, (struct sockaddr*)&srv_addr, &len);
                                    if(ret < 0){
                                        printf("[PEER][ERR]  Timeout scaduto, BOOT fallito!\n");
                                        goto riprova;
                                    }
                                } while (ret < 0);

                                
                                //salvo i neighbors
                                sscanf(msg,"%s %d %d",str1,&neighbor1,&neighbor2);
                                printf("[PEER][LOG]  Neighbors:  [%d]  [%d]\n",neighbor1,neighbor2);
                                //Controllo sul messaggio ricevuto
                                if(strcmp(str1,"ACK") == 0){
                                    printf("[PEER][LOG]  ACK ricevuto con successo!\n");
                                }else{
                                    printf("[PEER][ERR]  BOOT_ACK non ricevuto!\n");
                                }

                                //Setto il peer come connesso
                                connesso = 1;
                                printf("[PEER][LOG]  Connessione al network effettuata con successo!\n\n");

                            //TERMINAZIONE FASE DI BOOT
                                
                            }else{
                                printf("[PEER][ERR]  Formato non corretto\n");
                                continue;
                            }
                        }

                    }else if(!strcmp(comando,"add")){      //controllo che il comando sia uno tra quelli disponibili e che ogni volta il peer sia connesso

                            if((strlen(opzione1) != 0) && (strlen(opzione2) != 0) ){
                                
                                //variabile che conterrà opzione2 convertita come intero
                                int num;           

                                //testo se il type inserito è valido
                                if((strcmp(opzione1,"tampone") == 0)){      //add tampone num
                                    printf("[PEER][LOG]  Comando %s %s %s accettato\n", comando, opzione1, opzione2);
                                    strcpy(opzione1,"t");
                                    num = atoi(opzione2);
                                }else if(strcmp(opzione1,"nuovo") == 0 && strcmp(opzione2,"caso") == 0){    //add nuovo caso num
                                    printf("[PEER][LOG]  Comando %s %s %s %s accettato\n", comando, opzione1, opzione2, opzione3);
                                    strcpy(opzione1,"n");
                                    num = atoi(opzione3);
                                }else{
                                    printf("[PEER][ERR]  type non valido, type può essere: tampone o nuovo caso\n");
                                    continue;
                                }

                                
                                //inserisco la nuova entry nel registro giornaliero
                                inserisciEntry(opzione1,num,portaPeer);

                                //stampaRegGiornaliero();       DEBUG
                            }else{
                                printf("[PEER][ERR]  formato non corretto\n");
                                continue;
                            }
        

                    }else if(!strcmp(comando,"get")){      
                        if(connesso){
                            if((strlen(opzione1) != 0) && (strlen(opzione2) != 0)){

                                //Contiene la stringa relativa alla aggregazione richiesta
                                char str[45];

                                //contiene la data più remota passata
                                char data1[12];

                                //contiene la datta più recente passata
                                char data2[12];

                                //contiene un array di sottostringhe restituite dalla strtok()
                                char* token = 0;

                                //delimitatore per la divisione in sottostringhe
                                const char delimiter[] = "-";

                                //Variabile d'appoggio per la data odierna
                                char data[12];

                                //Variabile che vale 0 quando si hanno tutte le entry
                                int datefinite = 1;

                                //Variabile che contiene il tipo della entry da cercare
                                char tipo[2];

                                char operazione[45];

                                if(strcmp(opzione1,"totale") != 0 && strcmp(opzione1,"variazione")!= 0){
                                    printf("[PEER][ERR]  aggr non valido, aggr può essere: totale o variazione\n");
                                    continue;
                                }


                                if(strcmp(opzione2,"tampone") == 0){
                                
                                    if((strlen(opzione3) != 0)){
                                        printf("\n[PEER][LOG]  Comando %s %s %s %s accettato\n", comando, opzione1, opzione2, opzione3);
                                        token = strtok(opzione3,delimiter);
                                
                                            strcpy(data1,token);
    
                                            token = strtok(NULL, delimiter);
                                        
                                            strcpy(data2,token);

                                        //printf("%s %s\n",data1,data2);
                                
                                    
                                    }else{
                                        printf("\n[PEER][LOG]  Comando %s %s %s accettato\n", comando, opzione1, opzione2);
                                        strcpy(data1,"*");
                                        strcpy(data2,"*");
                                        //printf("%s %s\n",data1,data2);
                                    }

                                    sprintf(str,"%s %s %s %s",opzione1, opzione2, data1, data2);
                                    //printf("%s\n",str);

                                }else if(strcmp(opzione2,"nuovo") == 0 && strcmp(opzione3,"caso") == 0){
                                    if((strlen(opzione4) != 0)){
                                        printf("\n[PEER][LOG]  Comando %s %s %s %s %s accettato\n", comando, opzione1, opzione2, opzione3, opzione4);
                                        token = strtok(opzione4,delimiter);
                                
                                            strcpy(data1,token);
    
                                            token = strtok(NULL, delimiter);
                                        
                                            strcpy(data2,token);

                                        //printf("%s %s\n",data1,data2);
                                    }else{
                                        printf("\n[PEER][LOG]  Comando %s %s %s %s accettato\n", comando, opzione1, opzione2, opzione3);
                                        strcpy(data1,"*");
                                        strcpy(data2,"*");
                                        //printf("%s %s\n",data1,data2);
                                    }

                                    sprintf(str,"%s %s %s %s %s",opzione1, opzione2, opzione3, data1, data2);
                                    //printf("%s\n",str);
                                }else{
                                    printf("[PEER][ERR]  type non valido, type può essere: tampone o nuovo caso\n");
                                    continue;
                                }
                                //printf("SONO FUORI\n");

                                //CONTOLLO SE AGGR TYPE PERIOD C'È, ALTRIMENTI LO CHIEDO AI NEIGHBORS
                                /*inserisciAggr("totale tampone 20 20",3333);
                                inserisciAggr("variazione nuovo caso 30 15", 12);
                                inserisciAggr("variazione tampone 3 12", 0);*/

                                //Controllo se la data più recente è uguale a quella del registro aperto, in questo caso errore
                                getDate(data);
                
                                if(strcmp(data2,data) == 0){
                                    printf("[PEER][ERR]  La data si riferisce ad un registro ancora aperto!\n");
                                    continue;
                                }

                                //Se non è specificato l'upper bound allora gli assegno la data più recente 
                                //Se è specificato assegno a data, la data più recente
                                if(strcmp(data2,"*") == 0){
                                    getDate(data);
                                    decrementaData(data);
                                }else{
                                    strcpy(data,data2);
                                }

                                strcpy(operazione,str);

                                //Controllo se possiedo già il dato aggregato
                                ret = trovaAggr(str);

                                //Stampo il risultato
                                if(ret >= 0){
                                    printf("\nRisultato dell'aggregazione:    %d\n\n",ret);
                                    continue;
                                }else{
                                
                                    char *cmd = "REQ_DATA\0";

                                    printf("[PEER][LOG]  Aggregazione non trovata\n");
                                    //stampaAggr();

                                    //RICHIEDERE AI NEIGHBORS SE HANNO str;
                                    //INVIARE RICHIESTA, LEN E POI STR AD ENTRAMBI
                                    if(neighbor1 != 0){

                                        char valore[12];
                                        
                                        //pulizia
                                        memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                                        //informazioni sul peer
                                        connecting_addr.sin_family = AF_INET ;
                                        connecting_addr.sin_port = htons(neighbor1);
                                        inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);


                                        //Invio del REQ_DATA al N1
                                        printf("[PEER][LOG]  Invio REQ_DATA a %d\n",neighbor1);

                                        do
                                        {
                                            ret = sendto(sd, (void*)cmd, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                perror("Errore nell'invio del REQ_DATA");
                                                sleep(POLLING_TIME);
                                            }
                                        } while (ret < 0);

                                        //Invio lunghezza di str a n1
                                        printf("[PEER][LOG]  Invio lunghezza\n");

                                        len = strlen(str) + 1;
                                        lmsg = htons(len);
                                        
                                        do{
                                        ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                sleep(POLLING_TIME);
                                                perror("Errore nella sendto");
                                            }
                                        }while(ret < 0 );
                                        
                                        //Invio messaggio contenente la aggr
                                        do{
                                            ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                sleep(POLLING_TIME);
                                                perror("Errore nella sendto");
                                        }
                                        }while(ret < 0 );

                                        //Attendo la lunghezza del messaggio
                                        
                                        do
                                        {
                                            ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                            if(ret < 0){
                                                printf("[PEER][ERR]  lunghezza non ricevuta!\n");        
                                            }
                                        } while (ret < 0);

                                        size = ntohs(lmsg);
                                        //printf("SIZE RICEVUTA %d\n", size); //DEBUG

                                        if(size == 0){
                                            printf("[PEER][LOG]  Neighbor2 non ha il dato richiesto\n");
                                            goto contattaN2;
                                        }

                                        //attendo il messaggio
                                        do
                                        {
                                            ret = recvfrom(sd, (void*)&valore, size, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                            if(ret < 0){
                                                printf("[PEER][ERR]  Aggr non ricevuta!\n");        
                                            }
                                        } while (ret < 0);

                                        printf("\nRisultato dell'aggregazione:    %s\n\n",valore);

                                        ret = atoi(valore);

                                        
                                        inserisciAggr(str,ret);
                                        

                                        printf("[PEER][LOG]  Aggiornamento Registro delle aggregazioni\n");
                                        //stampaAggr();

                                        continue;

                                    }else{
                                        printf("[PEER][LOG]  Neighbor1 non presente\n");
                                    }

                                    contattaN2:
                                    if(neighbor2 != 0){

                                        char valore[12];
                                        
                                        //pulizia
                                        memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                                        //informazioni sul peer
                                        connecting_addr.sin_family = AF_INET ;
                                        connecting_addr.sin_port = htons(neighbor2);
                                        inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);


                                        //Invio del REQ_DATA al N1
                                        printf("[PEER][LOG]  Invio REQ_DATA a %d\n",neighbor2);

                                        do
                                        {
                                            ret = sendto(sd, (void*)cmd, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                perror("Errore nell'invio del REQ_DATA");
                                                sleep(POLLING_TIME);
                                            }
                                        } while (ret < 0);

                                        //Invio lunghezza di str a n1
                                        printf("[PEER][LOG]  Invio lunghezza\n");

                                        len = strlen(str) + 1;
                                        lmsg = htons(len);
                                        
                                        do{
                                        ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                sleep(POLLING_TIME);
                                                perror("Errore nella sendto");
                                            }
                                        }while(ret < 0 );
                                        
                                        //Invio messaggio contenente la aggr
                                        do{
                                            ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                sleep(POLLING_TIME);
                                                perror("Errore nella sendto");
                                        }
                                        }while(ret < 0 );

                                        //Attendo la lunghezza del messaggio
                                        
                                        do
                                        {
                                            ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                            if(ret < 0){
                                                printf("[PEER][ERR]  Lunghezza non ricevuta!\n");        
                                            }
                                        } while (ret < 0);

                                        size = ntohs(lmsg);
                                        //printf("SIZE RICEVUTA %d\n", size); //DEBUG

                                        if(size == 0){
                                            printf("[PEER][LOG]  Neighbor2 non ha il dato richiesto\n");
                                            goto flood;
                                        }

                                        //attendo il messaggio
                                        do
                                        {
                                            ret = recvfrom(sd, (void*)&valore, size, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                            if(ret < 0){
                                                printf("[PEER][ERR]  aggr non ricevuta!\n");        
                                            }
                                        } while (ret < 0);

                                        printf("\nRisultato dell'aggregazione:    %s\n\n",valore);

                                        ret = atoi(valore);

                                        inserisciAggr(str,ret);

                                        printf("[PEER][LOG]  Aggiornamento Registro delle aggregazioni\n");
                                        //stampaAggr();

                                        continue;

                                    }else{
                                        printf("[PEER][LOG]  Neighbor2 non presente\n");
                                    }

                                }

                                flood:
                                printf("[PEER][LOG]  Nessun neighbor ha il dato\n"); 

                                //Controllo se ho le entry per calcolare il dato, altrimento le richiedo, parto dalla data più recente
                                do{
                                    //Contatto il ds per conoscere i presenti di quel determinato giorno

                                    printf("\n[PEER][LOG]  Invio PRES_REQ al ds per la data %s\n",data);

                                    //imposto il messaggio di richiesta di disconnessione
                                    char* cmd = "PRES_REQ\0";
                                    char* ack = "ACK\0";
                                    int porta;
                                    int valore = -1;

                                    //pulizia
                                    memset(&srv_addr, 0, sizeof(srv_addr)); 
                                    //informazioni sul server
                                    srv_addr.sin_family = AF_INET ;
                                    srv_addr.sin_port = htons(portaDS);
                                    inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr);

                                    //Invio del PRES_REQ al DS
                                    do
                                    {
                                        ret = sendto(sd, cmd, REQ_LEN, 0,(struct sockaddr*)&srv_addr, sizeof(srv_addr));
                                        if(ret < 0){
                                            perror("Errore nell'invio del PRES_REQ");
                                            sleep(POLLING_TIME);
                                        }
                                    } while (ret < 0);

                                    printf("[PEER][LOG]  Invio data: %s\n",data);
                                    //Invio della data al DS
                                    do
                                    {
                                        ret = sendto(sd, data, DATE_LEN, 0,(struct sockaddr*)&srv_addr, sizeof(srv_addr));
                                        if(ret < 0){
                                            perror("Errore nell'invio della data");
                                            sleep(POLLING_TIME);
                                        }
                                    } while (ret < 0);
                                    

                                    do{
                                        valore = -1;
                                        //Attesa della lunghezza della porta dal DS
                                        do
                                        {
                                            ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&srv_addr, &len);
                                            if(ret < 0){
                                                printf("[PEER][ERR]  Lunghezza non ricevuta!\n");
                                            }
                                        } while (ret < 0);

                                        size = ntohs(lmsg);
                                        //printf("SIZE RICEVUTA %d\n", size); //DEBUG
                                        if(size == 0){
                                            datefinite = 0;
                                            porta = 0;
                                            continue;
                                        }

                                        do
                                        {
                                            ret = recvfrom(sd, (void*)&msg,size, 0, (struct sockaddr*)&srv_addr, &len);
                                            if(ret < 0){
                                                printf("[PEER][ERR]  Porta non ricevuta!\n");
                                        }
                                        } while (ret < 0);

                                        porta = atoi(msg);

                                        if(porta == 0){
                                            printf("[PEER][LOG]  Presenze giorno %s terminate\n",data);
                                            //Calcolo il dato aggregato relativo a questo giorno!   
                                            goto prossimaEntry;
                                        }

                                        printf("[PEER][LOG]  Porta %d ricevuta\n",porta);
                                        
                                        

                                        //CONTROLLO SE HO LE ENTRY!*****

                                        if(strcmp(opzione2,"tampone") == 0){
                                            strcpy(tipo, "t");
                                        }else{
                                            strcpy(tipo, "n");
                                        }

                                        //Controllo se ho già l'entry relativa alla data, tipo e porta passati
                                        if(entryTrovata(data,tipo,porta) >= 0){
                                            
                                            printf("[PEER][LOG]  Entry %s %s %d già presente\n",data,tipo,porta);

                                        }else{ 
                                            
                                            printf("[PEER][LOG]  Entry non trovata, occorre effettuare un flooding\n");

                                            //FLOOD_FOR_ENTRIES
                                            sprintf(str,"%s %s %d %d", data,tipo,porta,portaPeer);

                                            //Invio richiesta a n1 se fa
                                            if((neighbor1 != 0) && (neighbor1 <= portaPeer)){
                        
                                                char* cmd = "FLOOD_RQ\0";

                                                printf("[PEER][LOG]  Invio richiesta di FLOOD_FOR_ENTRIES al neighbor1 %d\n",neighbor1);

                                            //pulizia
                                                memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                                                //informazioni sul peer
                                                connecting_addr.sin_family = AF_INET ;
                                                connecting_addr.sin_port = htons(neighbor1);
                                                inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);

                                                do
                                                {
                                                ret = sendto(sd, (void*)cmd, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                                    if(ret < 0){
                                                        perror("Errore nell'invio del FLOOD_RQ");
                                                        sleep(POLLING_TIME);
                                                    }
                                                } while (ret < 0);

                                                //printf("[PEER][LOG]  Invio lunghezza \n");
                                    
                                                sprintf(str,"%s %s %d %d", data,tipo,porta,portaPeer);            

                                                //determino la lunghezza del messaggio
                                                len = strlen(str)+1;
                                                //printf("%d\n",len);
                                                lmsg = htons(len);

                                            //Invio lunghezza entry
                                            do{
                                                ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                                if(ret < 0){
                                                    sleep(POLLING_TIME);
                                                    perror("Errore nella sendto");
                                                }
                                            }while(ret < 0 );

                                            printf("[PEER][LOG]  Lunghezza inviata \n");

                                            //Invio messaggio contenente la entry
                                            do{
                                                ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                                if(ret < 0){
                                                    sleep(POLLING_TIME);
                                                    perror("Errore nella sendto");
                                                }
                                            }while(ret < 0 );

                                            printf("[PEER][LOG]  Richiesta %s inviata \n", str);

                                            printf("[PEER][LOG]  Lunghezza ricevuta\n");

                                            //Attendo la lunghezza del messaggio
                                            do
                                            {
                                                ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                                if(ret < 0){
                                                    printf("[PEER][ERR]  lunghezza non ricevuta!\n");        
                                                }
                                            } while (ret < 0);

                                            size = ntohs(lmsg);
                                            //printf("SIZE RICEVUTA %d\n", size); //DEBUG

                                            //attendo il messaggio
                                            do
                                            {
                                                ret = recvfrom(sd, (void*)&str,size, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                                if(ret < 0){
                                                    printf("[PEER][ERR]  messaggio di flood non ricevuto!\n");        
                                                }
                                            } while (ret < 0);

                                            printf("[PEER][LOG]  Valore ricevuto: %s\n",str);

                                            sscanf(str,"%d",&valore);

                        
                                        }
                    
                                            if(valore > 0){
                                                printf("[PEER][LOG]  Entry recuperata!\n");
                                                struct entry* nuova = (struct entry*)malloc(sizeof(struct entry));
                                                nuova->porta = porta;
                                                strcpy(nuova->date,data);
                                                strcpy(nuova->type,tipo);
                                                nuova->quantity = valore;

                                                inserisciRegGenerale(nuova);

                                                printf("[PEER][LOG]  Entry inserita nel Registro Generale\n");
                                                goto prossimaEntry;
                                            }
                                            
                                            sprintf(str,"%s %s %d %d", data,tipo,porta,portaPeer);

                                            

                                            //Invio richiesta a n2 se fa
                                            if((neighbor2 != 0) && (neighbor2 >= portaPeer)){

                                            char* cmd = "FLOOD_RQ\0";

                                            printf("[PEER][LOG]  Invio richiesta di FLOOD_FOR_ENTRIES al neighbor2 %d\n",neighbor2);

                                            //pulizia
                                            memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                                            //informazioni sul peer
                                            connecting_addr.sin_family = AF_INET ;
                                            connecting_addr.sin_port = htons(neighbor2);
                                            inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);

                                            do
                                            {
                                                ret = sendto(sd, (void*)cmd, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                                if(ret < 0){
                                                    perror("Errore nell'invio del FLOOD_RQ");
                                                    sleep(POLLING_TIME);
                                                }
                                            } while (ret < 0);

                                            //printf("[PEER][LOG]  Invio lunghezza \n");
                                    
                                            sprintf(str,"%s %s %d %d", data,tipo,porta,portaPeer);            

                                            //determino la lunghezza del messaggio
                                            len = strlen(str)+1;
                                            //printf("%d\n",len);
                                            lmsg = htons(len);

                                            //Invio lunghezza entry
                                            do{
                                                ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                                if(ret < 0){
                                                    sleep(POLLING_TIME);
                                                    perror("Errore nella sendto");
                                                }
                                            }while(ret < 0 );

                                            printf("[PEER][LOG]  Lunghezza inviata \n");

                                            //Invio messaggio contenente la entry
                                            do{
                                                ret = sendto(sd, (void*)&str, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                                if(ret < 0){
                                                    sleep(POLLING_TIME);
                                                    perror("Errore nella sendto");
                                                }
                                            }while(ret < 0 );

                                            printf("[PEER][LOG]  Richiesta %s inviata \n", str);

                                            printf("[PEER][LOG]  Lunghezza ricevuta\n");

                                            //Attendo la lunghezza del messaggio
                                            do
                                            {
                                                ret = recvfrom(sd, (void*)&lmsg,sizeof(uint16_t), 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                                if(ret < 0){
                                                    printf("[PEER][ERR]  lunghezza non ricevuta!\n");        
                                                }
                                            } while (ret < 0);

                                            size = ntohs(lmsg);
                                            //printf("SIZE RICEVUTA %d\n", size); //DEBUG

                                            //attendo il messaggio
                                            do
                                            {
                                                ret = recvfrom(sd, (void*)&str,size, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                                if(ret < 0){
                                                    printf("[PEER][ERR]  messaggio di flood non ricevuto!\n");        
                                                }
                                            } while (ret < 0);

                                            printf("[PEER][LOG]  Valore ricevuto: %s\n",str);

                                            sscanf(str,"%d",&valore);
                        
                                        }
                                        
                                            if(valore > 0){
                                                printf("[PEER][LOG]  Entry recuperata!\n");
                                                struct entry* nuova = (struct entry*)malloc(sizeof(struct entry));
                                                nuova->porta = porta;
                                                strcpy(nuova->date,data);
                                                strcpy(nuova->type,tipo);
                                                nuova->quantity = valore;

                                                inserisciRegGenerale(nuova);
                                                printf("[PEER][LOG]  Entry inserita nel Registro Generale\n");
                                                goto prossimaEntry;
                                            }
                                            

                                        }
                                        

                                        prossimaEntry:
                                        do{
                                            ret = sendto(sd, (void*)&ack, ACK_LEN, 0,(struct sockaddr*)&srv_addr, sizeof(srv_addr));
                                            if(ret < 0){
                                            sleep(POLLING_TIME);
                                            perror("Errore nella sendto");
                                        }
                                        }while(ret < 0 );

                                        if(porta == 0){
                                            printf("[PEER][LOG]  Presenze giorno %s terminate\n",data);
                                            //Calcolo il dato aggregato relativo a questo giorno!   

                                        }

                                    }while(porta != 0);

                                    
                                    //Decremento la data se non sono arrivato alla data richiesta
                                    if(strcmp(data,data1) != 0){
                                        decrementaData(data);
                                        continue;
                                    }

                                    //Se arrivo qui ho tutte le entry
                                    //printf("[PEER][LOG]  Entry recuperate, verrà calcolato il dato\n");
                                    break;

                                }while(datefinite != 0);

                                printf("[PEER][LOG]  Entry recuperate, verrà calcolato il dato\n");
                                //stampaRegGen();

                                printf("[PEER][LOG]  Calcolo %s in corso...\n",operazione);

                                if(strcmp(opzione1,"totale") == 0){

                                    //printf("calcolo totale\n");

                                    if(strcmp(opzione2,"tampone") == 0){

                                        //printf("calcolo totale tampone\n");

                                        ret = calcoloTotale(operazione,"t");

                                        if(ret < 0){

                                            printf("\nErrore:  aggregazione richiesta su date antecedenti alla creazione del network\n\n");
                                        }else{

                                        
                                            inserisciAggr(operazione,ret);
                                            

                                            printf("\nRisultato dell'aggregazione:    %d\n\n",ret);
                                        }

                                    }else{

                                        //printf("calcolo totale nuovo caso\n");

                                        ret = calcoloTotale(operazione,"n");

                                        if(ret < 0){

                                            printf("\nErrore:  aggregazione richiesta su date antecedenti alla creazione del network\n\n");
                                        
                                        }else{

                                            
                                            inserisciAggr(operazione,ret);
                                            
                                            

                                            printf("\nRisultato dell'aggregazione:    %d\n\n",ret);
                                        }
                                    }
                                }else{

                                    //printf("calcolo variazione\n");

                                    if(strcmp(opzione2,"tampone") == 0){
                                        
                                        //Variaile d'appoggio per permettere la gestione di più date
                                        char data[12];

                                        if( strcmp(data2,"*") == 0){
                                            
                                            getDate(data2);
                                            decrementaData(data2);
                                        }

                                        

                                        while( strcmp(data2,data1) != 0){
                                            
                                            
                                            strcpy(data,data2);
                                            decrementaData(data);
                                            if(dataTrovata(data) == 0){
                                                goto fuoriVT;
                                            }

                                            ret = calcoloVariazione(data,data2,"t");

                                            printf("\nVariazione tamponi nel periodo %s-%s: %d\n",data,data2,ret);

                                            sprintf(operazione,"variazione tampone %s %s",data,data2);

                                            if(trovaAggr(operazione) < 0){
                                                inserisciAggr(operazione,ret);
                                            }
                                            
                                            decrementaData(data2);
                                        }

                                        fuoriVT:      //fuori variazione tamponi

                                        
                                        printf("[PEER][LOG]  Calcolo aggregazione terminato\n");



                                    }else{
                                        //printf("calcolo variazione nuovo caso\n");
                                        
                                        //Variaile d'appoggio per permettere la gestione di più date
                                        char data[12];

                                        if( strcmp(data2,"*") == 0){
                                            
                                            getDate(data2);
                                            decrementaData(data2);
                                        }

                                        

                                        while( strcmp(data2,data1) != 0){
                                            
                                            
                                            strcpy(data,data2);
                                            decrementaData(data);
                                            if(dataTrovata(data) == 0){
                                                goto fuoriVNC;
                                            }

                                            ret = calcoloVariazione(data,data2,"n");

                                            printf("\nVariazione nuovi casi nel periodo %s-%s: %d\n",data,data2,ret);

                                            sprintf(operazione,"variazione nuovo caso %s %s",data,data2);

                                            if(trovaAggr(operazione) < 0){
                                                inserisciAggr(operazione,ret);
                                            }
                                            
                                            decrementaData(data2);
                                        }

                                        fuoriVNC:       //Fuori variazione nuovi casi

                                        printf("[PEER][LOG]  Calcolo aggregazione terminato\n");
                                    }
                                }
                                

                                //stampaAggr();




                            }else{
                                printf("[PEER][ERR]  formato non corretto\n");
                                continue;
                            }
                        }else{
                            printf("[PEER][ERROR]   peer non connesso al DS\n");
                            continue;
                        }

                    }else if(!strcmp(comando,"stop")){      
                        if(connesso){

                            if((strlen(opzione1) == 0) && (strlen(opzione2) == 0)){
                                printf("[PEER][LOG]  Comando %s accettato\n", comando);
                                char* cmd = "AD_ENTRY\0";
                                
                                struct entry* s;
                                //INVIO DI TUTTE LE ENTRY SALVATE AI PEER 
                                //-->CHIUDOREGGIORNALIERO-->INVIO LE ENTRY DI REG GENERALE AI NEIGHBORS.
                                chiusuraRegGornaliero();
                                printf("[PEER][LOG]  Reg giornaliero chiuso\n");
                                //INVIO A N1 DEL REGISTRO GENERALE
                                if(neighbor1 != 0){
                                    printf("[PEER][LOG]  Invio le entry al neighbor [%d]\n",neighbor1);
                                    //stampaRegGen();
                                    s = RegGenerale;

                                    //pulizia
                                    memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                                    //informazioni sul peer
                                    connecting_addr.sin_family = AF_INET ;
                                    connecting_addr.sin_port = htons(neighbor1);
                                    inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);

                                    while(s != NULL){
                                        //variabile che conterrà la entry in formato testuale
                                        char e[BUFFER_SIZE];
                                        char ack[4];

                                        //controllo che abbia informazioni non nulle
                                        //if(s->quantity == 0){
                                        //    s = s->next;
                                        //    continue;
                                        //}

                                        //imposto il comando da mandare
                                        
                                        printf("[PEER][LOG]  Tentativo di invio di AD_ENTRY...\n");

                                        //Invio del AD_ENTRY al N1
                                        do
                                        {
                                            ret = sendto(sd, (void*)cmd, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                perror("Errore nell'invio del ADD_ENTRY");
                                                sleep(POLLING_TIME);
                                            }
                                        } while (ret < 0);

                                        printf("[PEER][LOG]  Invio lunghezza entry \n");
                                    
                                        sprintf(e,"%s %s %d %d",s->date, s->type, s->quantity, s->porta);

                                        //printf("%s\n",e);
                                    
                                        //determino la lunghezza del messaggio
                                        len = strlen(e)+1;
                                        //printf("%d\n",len);
                                        lmsg = htons(len);

                                        //Invio lunghezza entry
                                        do{
                                        ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                sleep(POLLING_TIME);
                                                perror("Errore nella sendto");
                                            }
                                        }while(ret < 0 );

                                        printf("[PEER][LOG]  Lunghezza inviata \n");

                                        //Invio messaggio contenente la entry
                                        do{
                                            ret = sendto(sd, (void*)&e, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                sleep(POLLING_TIME);
                                                perror("Errore nella sendto");
                                        }
                                        }while(ret < 0 );

                                        printf("[PEER][LOG]  Entry %s inviata \n", e);

                                        printf("[PEER][LOG]  Attesa di risposta dal peer...!\n");

                                        //Attesa del ACK dal peer
                                        do
                                        {
                                            ret = recvfrom(sd, (void*)ack, ACK_LEN, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                            if(ret < 0){
                                                printf("[PEER][ERR]  Timeout scaduto, ACK non ricevuta!\n");
                                    
                                            }
                                        } while (ret < 0);

                                        //printf("%s\n",ack);
                                        if(strcmp(ack,"ACK") == 0){
                                           printf("[PEER][LOG]  ACK ricevuto");
                                        }

                                        s = s->next;

                                        printf("[PEER][LOG]  Invio entry completato\n");
                                    }

                                }

                                if(neighbor2 != 0){
                                    printf("[PEER][LOG]  Invio le entry al neighbor [%d]\n",neighbor2);
                                    //stampaRegGen();
                                    s = RegGenerale;

                                    //pulizia
                                    memset(&connecting_addr, 0, sizeof(connecting_addr)); 
                                    //informazioni sul peer
                                    connecting_addr.sin_family = AF_INET ;
                                    connecting_addr.sin_port = htons(neighbor2);
                                    inet_pton(AF_INET, "127.0.0.1", &connecting_addr.sin_addr);

                                    while(s != NULL){
                                        //variabile che conterrà la entry in formato testuale
                                        char e[BUFFER_SIZE];
                                        char ack[4];

                                        //controllo che abbia informazioni non nulle
                                        /*if(s->quantity == 0){
                                            s = s->next;
                                            continue;
                                        }*/

                                        //imposto il comando da mandare
                                        
                                        printf("[PEER][LOG]  Tentativo di invio di AD_ENTRY...\n");

                                        //Invio del AD_ENTRY al N1
                                        do
                                        {
                                            ret = sendto(sd, (void*)cmd, REQ_LEN, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                perror("Errore nell'invio del ADD_ENTRY");
                                                sleep(POLLING_TIME);
                                            }
                                        } while (ret < 0);

                                        printf("[PEER][LOG]  Invio lunghezza entry \n");
                                    
                                        sprintf(e,"%s %s %d %d",s->date, s->type, s->quantity, s->porta);

                                        //printf("%s\n",e);
                                    
                                        //determino la lunghezza del messaggio
                                        len = strlen(e)+1;
                                        //printf("%d\n",len);
                                        lmsg = htons(len);

                                        //Invio lunghezza entry
                                        do{
                                        ret = sendto(sd, (void*)&lmsg, sizeof(u_int16_t), 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                sleep(POLLING_TIME);
                                                perror("Errore nella sendto");
                                            }
                                        }while(ret < 0 );

                                        printf("[PEER][LOG]  Lunghezza inviata \n");

                                        //Invio messaggio contenente la entry
                                        do{
                                            ret = sendto(sd, (void*)&e, len, 0,(struct sockaddr*)&connecting_addr, sizeof(connecting_addr));
                                            if(ret < 0){
                                                sleep(POLLING_TIME);
                                                perror("Errore nella sendto");
                                        }
                                        }while(ret < 0 );

                                        printf("[PEER][LOG]  Entry %s inviata \n", e);

                                        printf("[PEER][LOG]  Attesa di risposta dal peer...!\n");

                                        //Attesa del ACK dal peer
                                        do
                                        {
                                            ret = recvfrom(sd, (void*)ack, ACK_LEN, 0, (struct sockaddr*)&connecting_addr, &addrlen);
                                            if(ret < 0){
                                                printf("[PEER][ERR]  Timeout scaduto, ACK non ricevuta!\n");
                                    
                                            }
                                        } while (ret < 0);

                                        //printf("%s\n",ack);
                                        if(strcmp(ack,"ACK") == 0){
                                           printf("[PEER][LOG]  ACK ricevuto");
                                        }

                                        s = s->next;

                                        printf("[PEER][LOG]  Invio entry completato\n");
                                    }

                                }
                                //stampaRegGiornaliero();       //DEBUG
                                //stampaRegGen();               //DEBUG
                                //MANDO RICHIESTA DI DISCONNESSIONE
                                
                                //imposto il messaggio di richiesta di disconnessione
                                cmd = "DISC_REQ\0";

                                //pulizia
                                memset(&srv_addr, 0, sizeof(srv_addr)); 
                               //informazioni sul server
                                srv_addr.sin_family = AF_INET ;
                                srv_addr.sin_port = htons(portaDS);
                                inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr);

                              
                                printf("[PEER][LOG]  Tentativo di invio di DISC_REQ...\n");
                                
                                //Invio del DISC_REQ al DS
                                do
                                {
                                    ret = sendto(sd, cmd, REQ_LEN, 0,(struct sockaddr*)&srv_addr, sizeof(srv_addr));
                                    if(ret < 0){
                                        perror("Errore nell'invio del DISC");
                                        sleep(POLLING_TIME);
                                    }
                                } while (ret < 0);
                                printf("[PEER][LOG]  DISC_REQ inviata con successo!\n");
                                printf("[PEER][LOG]  Attesa di risposta dal DS...!\n");
                                
                                //Attesa del ACK dal DS
                                do
                                {
                                    ret = recvfrom(sd, (void*)msg, ACK_LEN, 0, (struct sockaddr*)&srv_addr, &len);
                                    if(ret < 0){
                                        printf("[PEER][ERR]  Timeout scaduto, discconnessione fallita!\n");
                                    }
                                } while (ret < 0);

                                //printf("%s\n",msg);
                                if(strcmp(msg,"ACK") == 0){
                                    printf("[PEER][LOG]  Salvataggio registro generale\n");
                                    printf("[PEER][LOG]  Peer chiuso, arrivederci");
                                    salvaRegGenerale();
                                    return 0;
                                }

                            }else{
                                printf("[PEER][ERR]  formato non corretto\n");
                                continue;
                            }
                        }else{
                            printf("[PEER][ERR]  peer non connesso al DS\n");
                            continue;
                        }

                    //Se non fa parte del set dei comandi allora non è valido
                    }else{
                        printf("[PEER][ERR]  non è stato inserito un comando valido!\n");
                        stampaComandi();
                        continue;
                    }

            }//fine ramo stdin

        }//fine while(select)        
                   
        //Reinserisco sd e stdin nel set di lettura per eventuali richieste multiple
        FD_SET(sd, &read_fds);
        FD_SET(STDIN, &read_fds);

        //CONTROLLO CHE SIANO LE 18 PER LA CHIUSURA DEL REGISTRO GIORNALIERO
        getHour(&ora);
        if(ora == 18 && regChiuso == 0){
            chiusuraRegGornaliero();
            regChiuso = 1;
            printf("[PEER][LOG]  Chiusura Registro Giornaliero!\n");
        }else if( ora != 18 && regChiuso == 1){
            regChiuso = 0;
        }

    }//fine while generale
    printf("[PEER][LOG]   Socket %d chiuso\n", sd);
    close(sd);
    
    return 0; 
}