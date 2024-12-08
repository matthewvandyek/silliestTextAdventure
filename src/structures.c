#include "common.h"
#include "structures.h"

Session sessions[MAX_CLIENTS];
EscapeRoom rooms[MAX_ROOMS];


/* ========================== SESSION ========================= */
void init_sessions()
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        sessions[i].sockfd = FREE_SLOT;
        sessions[i].active = 0;
        sessions[i].registered = 0;
        sessions[i].playing = 0;
        sessions[i].room = FREE_SLOT;
        sessions[i].nitems = 0;
        memset(sessions[i].username, 0, FIELD_SIZE);
        memset(sessions[i].inventory, 0, CARRY_CAPACITY * sizeof(Object *));
        pthread_mutex_init(&sessions[i].mutex, NULL);
    }
}

void create_session(int client_socket)
{
    /**
     * @brief Trova prima posizione libera nell'array {sessions}
     * e ci inizializza una nuova sessione dentro.
     */
    int client = find_session(FREE_SLOT);

    if (client == FREE_SLOT)
    {
        printf("create_session: troppi client connessi\n");
        return;
    }

    sessions[client].sockfd = client_socket;
    sessions[client].active = 1;
    sessions[client].registered = 0;
    sessions[client].playing = 0;
    sessions[client].room = FREE_SLOT;
    sessions[client].nitems = 0;
    memset(sessions[client].username, 0, FIELD_SIZE);
    memset(sessions[client].inventory, 0, CARRY_CAPACITY * sizeof(Object *));
}

void destroy_session(int client)
{
    /**
     * @brief Distrugge la sessione del client.
     */
    sessions[client].sockfd = FREE_SLOT;
    sessions[client].active = 0;
    sessions[client].registered = 0;
    sessions[client].playing = 0;
    sessions[client].room = FREE_SLOT;

    sessions[client].nitems = 0;
    memset(sessions[client].username, 0, FIELD_SIZE);
    memset(sessions[client].inventory, 0, CARRY_CAPACITY * sizeof(Object *));
}

int find_session(int client_socket)
{
    /**
     * @brief Trova la sessione del client con il socket {client_socket}.
     */
    int i;
    
    for (i = 0; i < MAX_CLIENTS; i++) if (sessions[i].sockfd == client_socket) return i;
    
    return FREE_SLOT;
}

int find_session_by_username(const char *username)
{
    int i;
    
    for (i = 0; i < MAX_CLIENTS; i++) if (strcmp(sessions[i].username, username) == 0) return i;
    
    return FREE_SLOT;
}

void kickout_session(int client_socket)
{
    int client = find_session(client_socket);
    int nitems;

    if (client == -1)
    {
        send_response(client_socket, "server: non posso fare kickout, nessun client sul socket %d", client_socket);
        return;
    }
    else if (!sessions[client].playing)
    {
        send_response(client_socket, "server: non posso fare kickout, client sul socket %d non sta ancora giocando", client_socket);
        return;
    }

    pthread_mutex_lock(&sessions[client].mutex);
    sessions[client].room = FREE_SLOT;
    sessions[client].playing = 0;

    /* put back objects? */
    for (nitems = 0; nitems < sessions[client].nitems; nitems++)
    {
        sessions[client].inventory[nitems] = NULL;
    }
    sessions[client].nitems = 0;
    pthread_mutex_unlock(&sessions[client].mutex);

    send_response_exclusive(sessions[client].sockfd, "%s", EOG);
}
/* ============================================================ */


/* ======================= ESCAPEROOM ========================= */
void init_rooms()
{
    int i;
    for (i = 0; i < MAX_ROOMS; i++) init_room(&rooms[i]);
}

void init_room(EscapeRoom *er)
{
    if (er == NULL)
    {
        printf("init_room: la room passata come pointer non esistente\n");
        return;
    }

    er->nclients = 0;
    er->ntokens = 0;
    er->nsolved = 0;

    er->ttl = 0;

    er->locations = NULL;
    er->nlocs = 0;

    er->objects = NULL;
    er->nobjs = 0;
}

void create_room(int room)
{
    if (room > MAX_ROOMS)
    {
        printf("create_room: l'id della room %d must be between 0 and %d\n", room, MAX_ROOMS);
        return;
    }

    if (rooms[room].nclients)
    {
        printf("create_room: la room %d e' gia' attiva\n", room);
        return;
    }

    switch (room)
    {
        case 0:
            rooms[0] = create_room_0();
            break;
    }
}

void destroy_room(int room)
{
    EscapeRoom *er;
    int loc, obj, cli;
    if (room < 0 || room > MAX_ROOMS)
    {
        printf("destroy_room: l'ID %d di room deve essere tra 0 e %d", room, MAX_ROOMS);
        return;
    }

    er = &rooms[room];
    if (!rooms[room].nclients)
    {
        printf("destroy_room: la room %d e' gia' disattiva\n", room);
        return;
    }

    /* segnala al mutex di fermare il timer */
    pthread_mutex_lock(&er->mutex);
    er->stop_timer = 1;
    pthread_mutex_unlock(&er->mutex);

    for (cli = 0; cli < MAX_CLIENTS; cli++)
    {
        if (sessions[cli].room == room)
        {
            kickout_session(sessions[cli].sockfd);
        }
    }

    for (loc = 0; loc < er->nlocs; loc++)
    {
        destroy_location(er->locations[loc]);
        er->locations[loc] = NULL;
    }
    free(er->locations);
    er->locations = NULL;

    for (obj = 0; obj < er->nobjs; obj++)
    {
        destroy_object(er->objects[obj]);
        er->objects[obj] = NULL;
    }
    free(er->objects);
    er->objects = NULL;

    pthread_mutex_destroy(&er->mutex);
    init_room(er);
}

void *timer(void *room_index)
{
    int client, room;
    EscapeRoom *er;

    room = *(int*)room_index;
    er = &rooms[room];

    while (1)
    {
        sleep(1);
        pthread_mutex_lock(&er->mutex);
        if (er->stop_timer || er->ttl <= 0)
        {
            pthread_mutex_unlock(&er->mutex);
            printf("Il timer della room %d e' stato fermato\n", room);
            break;
        }

        er->ttl--;
        printf("room %d: mancano %d secondi\n", room, er->ttl);
        pthread_mutex_unlock(&er->mutex);
    }

    printf("Il timer e scattato, partita terminata!\n");

    for (client = 0; client < MAX_CLIENTS; client++)
    {
        if (sessions[client].playing && sessions[client].room == room)
        {
            /* timer-specific message: kickout potrebbe essere chiamata altrove */
            append_message(sessions[client].sockfd, "il tempo e finito!");

            /* kickout deve per forza mandare la sua risposta specifica EOG: considera i casi d'errore, si manderebbero due send_response se fai send_response qui */
            /* kickout_session(sessions[client].sockfd); */
        }
    }

    destroy_room(room);

    return NULL;
}

/* ========================= ROOM 0 =========================== */
void stampa_info(int client_socket, void* data)
{
    if (data == NULL)
    {
        char *response;
        int section;
        ssize_t nbytes;

        send_response_exclusive(client_socket, "Sezioni disponibili:\n\t1. Obbiettivi\n\t2. Locazione\n\t3. Stanza");
        
        response = NULL;
        nbytes = recv_message(client_socket, &response);

        if (nbytes == -1 || nbytes == -2) return;
        else section = atoi(response);

        switch (section) {
            case 1:
                append_message(client_socket, "Leggi: \"Si vuole permettere a due monaci a leggere e scrivere in specifici orari: a sinistra della finestra, dopo la terza fino alla sesta; a destra, dopo la sesta fino alla nona. Non e possibile usare la stanza dai due monaci in contemporanea.\"");
                break;
            case 2:
                append_message(client_socket, "Leggi: \"La stanza e posizionata a 48° Nord, con la finestra guardante verso Sud.\"");
                break;
            case 3:
                append_message(client_socket, "Leggi: \"Lunghezza e larghezza: circa 8 entrambe. La finestra parte da terra e finisce al tetto, alta e lunga 5, centrata nella parete.\"");
                break;
            /*case 4:
                append_message(client_socket, "Hai trovato un pezzo di carta e sbloccato una magia! Brucialo insieme ad una candela magica... A tuo rischio. Il vecchio ti potrebbe denunciare per eresia.");
                break;*/
            default:
                append_message(client_socket, "Il libro sembra quasi dire, annoiato: \"non ci siamo capiti. Le sezioni vanno da 1 a 3.\"");
                break;
        }
        send_response_exclusive(client_socket, "");
    }
}

void stampa_date(int client_socket, void* data)
{
    if (data == NULL)
    {
        send_response_exclusive(client_socket, "I monaci si sono stati segnati presenti tutto ottobre. Di mattina Jorge, e di pomeriggio Abbone.");
    }
}

void stampa_link(int client_socket, void* data)
{
    if (data == NULL)
    {
        send_response_exclusive(client_socket, "https://www.susdesign.com/light_penetration/index.php");
    }
}

void apri_cassaforte(int client_socket, void* data)
{
    if (data != NULL)
    {
        Object *chiave = (Object *)data;
        append_message(client_socket, "cassaforte aperta!\n");

        /* cambia stato dell'oggetto nascosto */
        chiave->state = FREE;
        send_response_exclusive(client_socket, "Hai trovato dentro una **chiave**!");
    }
}

void apri_locchetto(int client_socket, void* data)
{
    if (data == NULL)
    {
        append_message(client_socket, "locchetto aperto!\n");
        send_response_exclusive(client_socket, "Dentro il cassetto trovi una lettera dove il monaco Jorge confessa il suo delitto. La mostri al vecchio, che incredulo, ti lascia uscire.");
    }
}

EscapeRoom create_room_0()
{
    EscapeRoom er;
    int i;

    er.ttl = 50;
    er.stop_timer = 0;

    pthread_mutex_init(&er.mutex, NULL);

    /* start thread in start */

    pthread_mutex_lock(&er.mutex);
    
    strcpy(er.name, "Il nome dell'Anemone");
    strcpy(er.story, "Da qualche parte nel nord Italia, in un monastero sperduto, sei stato assoldato per un'impresa non banale: quella di capire chi dei due monaci che lavoravano nella stanza e responsabile della incriminata opera, \"Defensio Quaestionum Octo de Auctoritate Summi Pontificis\", considerata eretica.\n L'unica uscita dalla stanza dove ti trovi e protetta fedelmente da un vecchietto vestito in armatura a piastre. Il bibliotecario e chiaramente deciso a non farti uscire finche non risolvi il mistero...");
    
    er.nclients = 0;
    er.ntokens = 3;
    er.nsolved = 0;

    strcpy(er.description, "Sei entrato in una stanza di pietra. Davanti a te c'e l'unica ++finestra++, con vicino appeso un **calendario**. La luce si diffonde appena; noti che fa troppo buio per scrivere ove i suoi raggi non giungono direttamente. Dal lato destro della ++finestra++ si trova una ++scrivania++, dal sinistro un ++tavolo++.\nSulla ++scrivania++ giace un **manoscritto**, sul tavolo una **cassaforte**.\nDietro di te c'e un ++scaffale++, dove sai che c'e un **libro** contenente dati sulla stanza.");
    
    er.nlocs = 4;
    er.locations = (Location **)malloc(er.nlocs * sizeof(Location *));
    for (i = 0; i < er.nlocs; i++) {
        er.locations[i] = (Location *)malloc(sizeof(Location));
    }

    er.nobjs = 6;
    er.objects = (Object **)malloc(er.nobjs * sizeof(Object *));
    for (i = 0; i < er.nobjs; i++) {
        er.objects[i] = (Object *)malloc(sizeof(Object));
    }

    /* Initialize Locations */

    /* Location 0 - Finestra */
    strcpy(er.locations[0]->name, "finestra");
    strcpy(er.locations[0]->description, "Una grande finestra che illumina la stanza, con un **calendario** appeso vicino.");

    /* Location 1 - Tavolo */
    strcpy(er.locations[1]->name, "tavolo");
    strcpy(er.locations[1]->description, "Un tavolo di legno con una **cassaforte** sopra di esso. Ha inoltre un cassetto chiuso da un **locchetto**.");

    /* Location 2 - Scrivania */
    strcpy(er.locations[2]->name, "scrivania");
    strcpy(er.locations[2]->description, "Una scrivania polverosa; ci sono tanti fogli sopra, ma ti colpisce in particolare un **manoscritto** al suo centro, per dei disegni colorati che contiene.");

    /* Location 3 - Scaffale */
    strcpy(er.locations[3]->name, "scaffale");
    strcpy(er.locations[3]->description, "Uno scaffale che copre tutta la parete, interrotto solo dalla porta.");

    /* Initialize Objects */

    /* Oggetto 0 - Calendario */
    er.objects[0]->state = FREE;
    strcpy(er.objects[0]->name, "calendario");
    strcpy(er.objects[0]->description, "Un calendario su cui i monaci si segnano la presenza.");
    /* strcpy(er.objects[0]->description, "Un calendario antico che segna dei giorni intorno all'equinozio di settembre, Anno Dominis 1327.\nTi viene in mente che la declinazione solare, solitamente notata con delta, e intorno a zero."); */
    er.objects[0]->enigma = NULL;
    er.objects[0]->effect = stampa_date;
    er.objects[0]->affected = NULL;

    /* Oggetto 1 - Cassaforte */
    er.objects[1]->state = BLOCKED;
    strcpy(er.objects[1]->name, "cassaforte");
    /* strcpy(er.objects[1]->description, "Una cassaforte di legno. Ha sopra un strano meccanismo che permette di scegliere unacombinazione di sette lettere. Le possibili lettere per la prima scelta sono maiuscole, mentre le altre tutte minuscole. Sara un nome?"); */
    strcpy(er.objects[1]->description, "Una cassaforte di legno. Ha sopra un strano meccanismo che permette di scegliere una combinazione di lettere, di lunghezza variabile. Implementazione del meccanismo lasciata come esercizio (di immaginazione) per il lettore.");
    
    /* Oggetto 1 - Cassaforte - Enigma */
    er.objects[1]->enigma =(Enigma*)malloc(sizeof(Enigma));
    er.objects[1]->enigma->type = MULTICHOICE;
    strcpy(er.objects[1]->enigma->data.multichoice.question, "Chi e l'autore del manoscritto?");
    er.objects[1]->enigma->data.multichoice.noptions = 2;
    er.objects[1]->enigma->data.multichoice.options = (char**)malloc(2*sizeof(char*));
    er.objects[1]->enigma->data.multichoice.options[0] = (char*)malloc(BUFFER_SIZE*sizeof(char));
    strcpy((er.objects[1]->enigma)->data.multichoice.options[0], "Jorge");
    er.objects[1]->enigma->data.multichoice.options[1] = (char*)malloc(BUFFER_SIZE*sizeof(char));
    strcpy((er.objects[1]->enigma)->data.multichoice.options[1], "Abbone");
    er.objects[1]->enigma->data.multichoice.correct_option = 1;
    er.objects[1]->enigma->bonus_time = 30;

    er.objects[1]->effect = apri_cassaforte;
    er.objects[1]->affected = NULL;

    /* Oggetto 2 - Manoscritto */
    er.objects[2]->state = BLOCKED;
    strcpy(er.objects[2]->name, "manoscritto");
    strcpy(er.objects[2]->description, "Ti avvicini al manoscritto: illustra un cane con tre teste che guardia l'entrata ad un posto infernale. Ma non hanno paura a portare alla luce del sole tali immagini, questi monaci?!");
    
    /* Oggetto 2 - Manoscritto - Enigma */
    er.objects[2]->enigma = (Enigma*)malloc(sizeof(Enigma));
    (er.objects[2]->enigma)->type = RIDDLE;
    strcpy((er.objects[2]->enigma)->data.riddle.question, 
       "D'improvviso, leggi sono uno dei disegni:\n"
       "\"Dove s'incantrano catarsi e imitazione\n"
       "In che risorsa ne trovi lezioni?\"");
    strcpy((er.objects[2]->enigma)->data.riddle.answer, "Poetica");
    er.objects[2]->enigma->bonus_time = 60;
    er.objects[2]->effect = stampa_link;
    er.objects[2]->affected = NULL;

    /* Oggetto 3 - Libro */
    er.objects[3]->state = FREE;
    strcpy(er.objects[3]->name, "libro");
    strcpy(er.objects[3]->description, "Un libro polveroso con informazioni riguardanti dimensioni e altri aspetti fisici della stanza.");
    er.objects[3]->enigma = NULL;
    er.objects[3]->effect = stampa_info;
    er.objects[3]->affected = NULL;

    /* Oggetto 4 - Locchetto */
    er.objects[4]->state = BLOCKED;
    strcpy(er.objects[4]->name, "locchetto");
    strcpy(er.objects[4]->description, "Seppur arruginito, sembra determinato a fare il suo lavoro. Pensi che serve proprio una chiava.");
    
    /* Oggetto 4 - Locchetto - Enigma [COMBO CON 5] */
    er.objects[4]->enigma = (Enigma*)malloc(sizeof(Enigma));
    er.objects[4]->enigma->type = COMBO;
    er.objects[4]->enigma->data.combo.other = NULL; /* per adesso, lo riconnetti dopo aver creato l'oggetto 5 */
    er.objects[4]->effect = apri_locchetto;
    er.objects[4]->affected = NULL;

    /* Oggetto 5 - Chiave */
    er.objects[5]->state = HIDDEN;
    strcpy(er.objects[5]->name, "chiave");
    strcpy(er.objects[5]->description, "Magnificamente arrugginata e determinata a resistere la sua eta. Ti ricorda di qualcosa?");
    
    /* Oggetto 5 - Chiave - Enigma [COMBO CON 4] */
    er.objects[5]->enigma = (Enigma*)malloc(sizeof(Enigma));
    er.objects[5]->enigma->type = COMBO;
    er.objects[5]->enigma->data.combo.other = NULL; /* per adesso, lo riconnetti dopo aver creato l'oggetto 5 */
    er.objects[5]->effect = apri_locchetto;
    er.objects[5]->affected = NULL;

    /* Connessione oggetti 4+5 */
    er.objects[4]->enigma->data.combo.other = er.objects[5];
    er.objects[5]->enigma->data.combo.other = er.objects[4];

    /* Connessione affected di 1 (cassaforte) e 5 (chiave) */
    er.objects[1]->affected = er.objects[5];

    pthread_mutex_unlock(&er.mutex);

    return er;
}

/* ============================================================ */


/* ======================== LOCATION ========================== */
Location *find_location(int room, char *loc)
{
    /**
     * @brief Trova la location {loc} nella room {room}.
     */
    int i;
    EscapeRoom *er = &rooms[room];

    for (i = 0; i < er->nlocs; i++)
    {
        if (strcmp(er->locations[i]->name, loc) == 0) return er->locations[i];
    }

    return NULL;
}

void destroy_location(Location *loc)
{
    if (loc == NULL)
    {
        printf("destroy_location: location non esistente\n");
        return;
    }

    free(loc);
}
/* ============================================================ */


/* ========================= OBJECT =========================== */
void destroy_object(Object *obj)
{
    if (obj == NULL)
    {
        printf("destroy_object: oggetto non esistente\n");
        return;
    }

    if (obj->enigma != NULL)
    {
        destroy_enigma(obj->enigma);
        obj->enigma = NULL;


        if (obj->affected)
        {
            free(obj->affected);
            obj->affected = NULL;
        }
    }

    free(obj);
}

Object *find_object(int room, char *obj)
{
    /**
     * @brief Trova l'oggetto {obj} nella room {room}.
     */
    int i;
    EscapeRoom *er = &rooms[room];

    for (i = 0; i < er->nobjs; i++)
    {
        if (strcmp(er->objects[i]->name, obj) == 0) return er->objects[i];
    }

    return NULL;
}

Object *find_object_in_inventory(int client, char *obj)
{
    /**
     * @brief Trova l'oggetto {obj} nell'inventario del client {client}.
     */
    int i;
    Session *s = &sessions[client];

    for (i = 0; i < s->nitems; i++)
    {
        if (strcmp(s->inventory[i]->name, obj) == 0) return s->inventory[i];
    }

    return NULL;
}

Object* remove_object(int room, const char* target)
{
    Object *obj = NULL;
    int i, j;
    
    for (i = 0; i < rooms[room].nobjs; i++)
    {
        if (strcmp(rooms[room].objects[i]->name, target) == 0)
        {
            obj = rooms[room].objects[i];

            /* shifta oggetti rimanenti */
            for (j = i; j < rooms[room].nobjs - 1; j++)
            {
                rooms[room].objects[j] = rooms[room].objects[j + 1];
            }

            /* rimuovi l'ultimo */
            rooms[room].objects[rooms[room].nobjs - 1] = NULL;
            rooms[room].nobjs--;
            break;
        }
    }
    return obj;
}
/* ============================================================ */


/* ========================= ENIGMA =========================== */
void destroy_enigma(Enigma *enigma)
{
    int i;
    switch (enigma->type)
    {
        case MULTICHOICE:
            for (i = 0; i < enigma->data.multichoice.noptions; i++)
            {
                free(enigma->data.multichoice.options[i]);
                enigma->data.multichoice.options[i] = NULL;
            }
            free(enigma->data.multichoice.options);
            enigma->data.multichoice.options = NULL;
            break;
        case ORDER:
            for (i = 0; i < enigma->data.order.nitems; i++)
            {
                free(enigma->data.order.objects[i]);
                enigma->data.order.objects[i] = NULL;
            }
            free(enigma->data.order.objects);
            enigma->data.order.objects = NULL;
            break;
        default:
            break;
    }

    free(enigma);
}

int solve_multichoice(int client_socket, Enigma *enigma)
{
    int opt;
    char *buffer = (char *)malloc(BUFFER_SIZE);
    if (buffer == NULL) 
    {
        send_response(client_socket, "err di allocazione della memoria");
        return -1;
    }

    append_message(client_socket, "Domanda: %s\n", enigma->data.multichoice.question);
    for (opt = 0; opt < enigma->data.multichoice.noptions; opt++)
    {
        append_message(client_socket, "\t%d) %s\n", opt + 1, enigma->data.multichoice.options[opt]);
    }
    send_response_exclusive(client_socket, "");

    memset(buffer, 0, BUFFER_SIZE);
    recv_message(client_socket, &buffer);

    if (atoi(buffer) == enigma->data.multichoice.correct_option)
    {
        append_message(client_socket, "Risposta corretta!\n");
        free(buffer);
        return 1;
    }
    else
    {
        append_message(client_socket, "Risposta sbagliata!\n");
        free(buffer);
        return 0;
    }

    return -1;
}

int solve_riddle(int client_socket, Enigma *enigma)
{
    char *buffer = (char *)malloc(BUFFER_SIZE);
    if (buffer == NULL) 
    {
        send_response(client_socket, "err di allocazione della memoria");
        return -1;
    }

    memset(buffer, 0, BUFFER_SIZE);

    append_message(client_socket, "Domanda: %s\n", enigma->data.riddle.question);

    send_response_exclusive(client_socket, "");

    recv_message(client_socket, &buffer);

    if (strcmp(buffer, enigma->data.riddle.answer) == 0)
    {
        append_message(client_socket, "Risposta corretta!\n");
        free(buffer);
        return 1;
    }
    else
    {
        append_message(client_socket, "Risposta sbagliata!\n");
        free(buffer);
        return 0;
    }
}

int solve_enigma(int client_socket, Enigma *enigma)
{
    switch (enigma->type)
    {
        case MULTICHOICE:
            return solve_multichoice(client_socket, enigma);
        case RIDDLE:
            return solve_riddle(client_socket, enigma);
        default:
            return -1;
    }
}
/* ============================================================ */
