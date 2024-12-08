#include "common.h"
#include "structures.h"
#include "authentication.h"
#include "implementation.h"

void init_data()
{
    init_sessions();
    init_rooms();
}

extern Session sessions[MAX_CLIENTS];
extern EscapeRoom rooms[MAX_ROOMS];


int won_game(int room)
{
    if (rooms[room].ntokens == rooms[room].nsolved) return 1;
    else return 0;
}

void handle_enigma(int client_socket, int room, Object *obj1, Object *obj2)
{
    int res = -1;

    printf("take: **%s** sbloccabile tramite un enigma\n", obj1->name);
    append_message(client_socket, "Oggetto **%s** e bloccato. Devi risolvere l'enigma!\n", obj1->name);

    /* obj2 prevede la COMBO */

    if (obj1->enigma == NULL || (obj1->enigma->type == COMBO && obj2 == NULL))
    {
        send_response(client_socket, "use: %s e bloccato, devi risolvere un enigma... che pero' non sta qui", obj1->name);
        return;
    }

    if (obj1->enigma->type == COMBO && obj2 != NULL)
    {
        /* use obj1 obj2 */
        if (obj2->state == HIDDEN)
        {
            send_response(client_socket, "use: l'esistenza di %s e al momento altamente dibattuta", obj2->name);
            return;
        }

        if (obj1->enigma->data.combo.other != obj2)
        {
            send_response(client_socket, "use: tu ci provi. ma non succede nulla");
            return;
        }

        /* combo giusta */

        res = 1;
    }
    else if (obj2 == NULL) res = solve_enigma(client_socket, obj1->enigma);

    if (res == 1) /* enigma risolto */
    {
        pthread_mutex_lock(&rooms[room].mutex);
        rooms[room].ttl += obj1->enigma->bonus_time;
        rooms[room].nsolved++;

        obj1->state = FREE;
        destroy_enigma(obj1->enigma);
        obj1->enigma = NULL;
        
        if (rooms[room].nsolved == rooms[room].ntokens)
        {
            int cli;

            rooms[room].nclients = 0;
            /* adesso non possono piu arrivare richieste che modif la room */
            pthread_mutex_unlock(&rooms[room].mutex);

            for (cli = 0; cli < MAX_CLIENTS; cli++)
            {
                if (sessions[cli].playing && sessions[cli].room == room)
                {
                    send_response_exclusive(sessions[cli].sockfd, "Congratulazioni! Hai vinto la partita!");
                    kickout_session(sessions[cli].sockfd);
                }
            }

            destroy_room(room);
            return;
        }
    
        pthread_mutex_unlock(&rooms[room].mutex);
        send_response_exclusive(client_socket, "enigma risolto! Hai sbloccato %s, fai un ulteriore take per prenderlo o use per attivare il suo eventuale effetto", obj1->name);

    }
}



/* ==================================================================================================================== */
/* ===================================================== COMANDI ====================================================== */
/* ==================================================================================================================== */
void handle_start(int client_socket, int room)
{
    int client = find_session(client_socket);

    if (!sessions[client].active)
    {
        send_response(client_socket, "start <room>: il client_socket non e valido");
        return;
    }

    if (!sessions[client].registered)
    {
        send_response(client_socket, "start <room>: devi prima fare il login");
        return;
    }

    if (sessions[client].playing)
    {
        send_response(client_socket, "start <room>: ma sei gia' in una partita, basta");
        return;
    }

    if (room < 0 || room > MAX_ROOMS)
    {
        send_response(client_socket, "start <room>: le room vanno da 0 a %d", MAX_ROOMS);
        return;
    }

    /* qui siamo tranquilli */
    
    /* room esiste e tutto, ma possiamo crearla? vedi: casi_nuovo_client.png */
    if (rooms[room].ttl == 0 && rooms[room].nclients == 0)
    {
        EscapeRoom *er;
        int room_index; /* perche room e un parametro, e devo passarci l'ind in pthread_create*/

        create_room(room);

        er = &rooms[room];
        room_index = room;
        
        if (pthread_create(&(er->timer_thread), NULL, timer, &room_index) != 0)
        {
            send_response(client_socket, "start <room>: failed to create timer thread");
            destroy_room(room);
            return;
        }

        pthread_detach(er->timer_thread);
    }
    else if (rooms[room].ttl == 0 && rooms[room].nclients > 0)
    {
        /* STOP! La room e' in fase di chiusura */
        send_response(client_socket, "start <room>: la room %d in fase di chiusura, aspetta", room);
        return;
    }

    add_client(room, client);

    send_response_exclusive(client_socket, "%s\n%s", rooms[room].name, rooms[room].story);
}

void handle_look(int client_socket, char *target)
{
    Location *loc = NULL;
    Object *obj = NULL;
    const int client = find_session(client_socket);
    const int room = sessions[client].room;

    /* error cases */
    if (!sessions[client].active)
    {
        send_response(client_socket, "start <room>: il client_socket non e valido");
        return;
    }
    else if (!sessions[client].registered)
    {
        send_response(client_socket, "look: devi fare login per usare questo comando");
        return;
    }
    else if (!sessions[client].playing)
    {
        send_response(client_socket, "look: devi iniziare una partita con start per usare questo comando");
        return;
    }
    /* fine controlli */

    /* look */
    if (target == NULL)
    {
        /* stampa info sulla stanza */
        send_response_exclusive(client_socket, "%s", rooms[room].description);
        return;
    }

    /* info devono essere consistenti se per caso ci passa un timer */
    pthread_mutex_lock(&rooms[room].mutex);

    loc = find_location(room, target);
    obj = find_object(room, target);

    /* look <target> */
    if (loc != NULL) send_response_exclusive(client_socket, "%s", loc->description);
    else if (obj != NULL && obj->state != HIDDEN) send_response_exclusive(client_socket, "%s", obj->description);
    else send_response(client_socket, "look: %s non esiste", target);

    pthread_mutex_unlock(&rooms[room].mutex);
}

void handle_take(int client_socket, char *target)
{
    int client, room;
    Object *obj;

    client = find_session(client_socket);
    if (client < 0 || client >= MAX_CLIENTS || !sessions[client].active)
    {
        send_response(client_socket, "take: il client_socket non e valido");
        return;
    }

    room = sessions[client].room;

    if (!sessions[client].registered)
    {
        send_response(client_socket, "take: devi prima fare login");
        return;
    }

    if (!sessions[client].playing)
    {
        send_response(client_socket, "take: devi iniziare una partita con start");
        return;
    }

    obj = find_object(room, target);
    if (obj == NULL)
    {
        send_response(client_socket, "take: %s non esiste", target);
        return;
    }

    if (obj->state == HIDDEN)
    {
        send_response(client_socket, "take: l'esistenza di %s e molto dibattuta e dipendente dalla tua epistemologia, tu cerca!", target);
        return;
    }

    if (obj->state == BLOCKED) /* enigma! */
    {

        handle_enigma(client_socket, room, obj, NULL);

        /* send_response(client_socket, ""); */
        return;
    }

    take_object(client_socket, target);
    send_response(client_socket, "take: oggetto **%s** nell'inventory di %s", target, sessions[client].username);
}

void handle_use_single(int client_socket, Object *obj1)
{
    /* obj1 esiste per forza se siamo qui */

    if (obj1->state == BLOCKED)
    {
        if (obj1->enigma == NULL)
        {
            send_response(client_socket, "use: %s e bloccato, devi risolvere un enigma... che pero' non sta qui", obj1->name);
        }
        else if (obj1->enigma->type != COMBO)
        {
            int ris;
            int room = sessions[find_session(client_socket)].room;

            append_message(client_socket, "use: %s e bloccato, devi risolvere l'enigma", obj1->name);
            ris = solve_enigma(client_socket, obj1->enigma);

            if (ris == 1) /* risolto l'enigma */
            {
                pthread_mutex_lock(&rooms[room].mutex);
                rooms[room].ttl += obj1->enigma->bonus_time;
                rooms[room].nsolved++;
                pthread_mutex_unlock(&rooms[room].mutex);

                obj1->state = FREE;
                destroy_enigma(obj1->enigma);
                obj1->enigma = NULL;
                
                if (won_game(room))
                {
                    int cli;

                    for (cli = 0; cli < MAX_CLIENTS; cli++)
                    {
                        if (sessions[cli].playing && sessions[cli].room == room)
                        {
                            send_response_exclusive(sessions[cli].sockfd, "Congratulazioni! Hai vinto la partita!");
                            kickout_session(sessions[cli].sockfd);
                        }
                    }
                    destroy_room(room);
                    return;
                }
                else send_message(client_socket, "congratulazioni! hai guadagnato un token e del tempo bonus");
                
            }
            
        }
        return;
    }

    if (obj1->state == FREE)
    {
        if (obj1->effect == NULL)
        {
            send_response(client_socket, "use: hmm, forse no...");
            return;
        }
        else
        {
            append_message(client_socket, "use: %s\n", obj1->name);

            printf("obj1->affected: %s\n", ((Object*)obj1->affected)->name);

            obj1->effect(client_socket, obj1->affected);
            return;
        }
    }
}

void handle_use_double(int client_socket, Object *obj1, Object *obj2)
{
    /* obj1 e obj2 esistono per forza */
    Object *main, *other;

    if (obj1->enigma && obj1->enigma->type == COMBO)
    {
        main = obj1;
        other = obj2;
    }
    else if (obj2->enigma && obj2->enigma->type == COMBO)
    {
        main = obj2;
        other = obj1;
    }
    else
    {
        send_response(client_socket, "use: non c'e' niente da fare con %s e %s", obj1->name, obj2->name);
        return;
    }

    if (main->enigma->data.combo.other == other)
    {
        int room = sessions[find_session(client_socket)].room;
        append_message(client_socket, "use: %s e %s\n", main->name, other->name);
        
        if (main->state == BLOCKED)
        {
            main->state = FREE;
            if (main->effect) main->effect(client_socket, main->affected);
        }
        if (other->state == BLOCKED)
        {
            other->state = FREE;
            if (other->effect) other->effect(client_socket, other->affected);
        }

        pthread_mutex_lock(&rooms[room].mutex);
        rooms[room].ttl += main->enigma->bonus_time;
        rooms[room].nsolved++;
        
        if (rooms[room].nsolved == rooms[room].ntokens)
        {
            /* end game in win! */
            send_message(client_socket, "congratulazioni! Hai vinto!");
            destroy_room(room);
            return;
        }
        else send_message(client_socket, "congratulazioni! hai guadagnato un token e del tempo bonus");
        pthread_mutex_unlock(&rooms[room].mutex);
    }
    else
    {
        send_response(client_socket, "use: %s e %s non vanno insieme... ma forse uno di loro con qualcos'altro?", obj1->name, obj2->name);
    }
}

void handle_use(int client_socket, char *target1, char *target2)
{
    Object *obj1, *obj2;
    int client;

    if (client_socket < 0)
    {
        send_response(client_socket, "use: il client_socket non e valido");
        return;
    }
    client = find_session(client_socket);

    if (target1 == NULL)
    {
        send_response(client_socket, "use: devi specificare almeno un oggetto");
        return;
    }

    /* ci assicuriamo almeno il primo oggetto esiste */

    if ((obj1 = find_object(sessions[client].room, target1)) == NULL && (obj1 = find_object_in_inventory(client, target1)) == NULL)
    {
        send_response(client_socket, "use: %s non esiste", target1);
        return;
    }

    if (target2 == NULL) handle_use_single(client_socket, obj1);
    else
    {
        if ((obj2 = find_object(sessions[client].room, target2)) == NULL && (obj2 = find_object_in_inventory(client, target2)) == NULL)
        {
            send_response(client_socket, "use: %s non esiste", target2);
            return;
        }

        handle_use_double(client_socket, obj1, obj2);
    }
}

void handle_objs(int client_socket)
{
    int client;
    int i;

    if (client_socket < 0)
    {
        send_response(client_socket, "objs: il client_socket non e valido");
        return;
    }

    client = find_session(client_socket);

    if (client < 0 || client >= MAX_CLIENTS || !sessions[client].active)
    {
        send_response(client_socket, "objs: il client_socket non e valido");
        return;
    }

    if (!sessions[client].registered)
    {
        send_response(client_socket, "objs: devi prima fare login");
        return;
    }

    if (!sessions[client].playing)
    {
        send_response(client_socket, "objs: devi iniziare una partita con start");
        return;
    }

    append_message(client_socket, "inventory di %s", sessions[client].username);
    if (sessions[client].nitems == 0)
    {
        append_message(client_socket, " e vuoto");
        send_message(client_socket, "");
        return;
    }
    else append_message(client_socket, ":\n");
    
    for (i = 0; i < sessions[client].nitems; i++)
    {
        if (i == sessions[client].nitems-1) append_message(client_socket, "\t**%s**", sessions[client].inventory[i]->name);
        else append_message(client_socket, "\t**%s**\n", sessions[client].inventory[i]->name);
    }
    send_message(client_socket, "");
}

void handle_end(int client_socket)
{
    int client = find_session(client_socket);
    int room;

    if (client < 0 || client >= MAX_CLIENTS || !sessions[client].active)
    {
        send_response(client_socket, "end: il client_socket non e valido");
        return;
    }

    if (!sessions[client].registered)
    {
        send_response(client_socket, "end: devi prima fare login e iniziare una partita");
        return;
    }

    if (!sessions[client].playing)
    {
        send_response(client_socket, "end: non sei in una partita");
        return;
    }

    room = sessions[client].room;

    pthread_mutex_lock(&rooms[room].mutex);
    rooms[room].nclients--;
    pthread_mutex_unlock(&rooms[room].mutex);

    append_message(client_socket, "end: hai lasciato la partita (puoi ritornare con start)");
    kickout_session(client_socket);

}
/* ==================================================================================================================== */
/* ==================================================================================================================== */
/* ==================================================================================================================== */



/* ==================================================================================================================== */
/* =============================================== INERFACCIA CON CLIENT ============================================== */
/* ==================================================================================================================== */
void create_client(int client_socket)
{
    /**
     * @brief Funzione atomica di interfaccia con il server,
     * crea un nuovo client con il socket {client_socket}.
     * @note Non serve un mutex perche' i timer non creano client.
     */
    create_session(client_socket);
}

void add_client(int room, int client)
{
    /**
     * @brief Funzione atomica di interfaccia con il server,
     * aggiunge il client {client} alla room {room}.
     * @note Non faccio troppi controlli tanto viene chiamata
     * in funzioni che devono fare quei controlli prima.
     */
    
    pthread_mutex_lock(&rooms[room].mutex);
    rooms[room].nclients++;
    pthread_mutex_unlock(&rooms[room].mutex);

    pthread_mutex_lock(&sessions[client].mutex);
    sessions[client].room = room;
    sessions[client].playing = 1;
    sessions[client].nitems = 0;

    /* eventually add other things */
    
    pthread_mutex_unlock(&sessions[client].mutex);
}

void remove_client(int client_socket)
{
    /**
     * @brief Funzione atomica di interfaccia con il server,
     * rimuove il client con il socket {client_socket}.
     */
    int client = find_session(client_socket);

    if (client == -1)
    {
        printf("remove_client: nessun client sul socket %d\n", client_socket);
        return;
    }

    pthread_mutex_lock(&sessions[client].mutex);
    destroy_session(client);
    pthread_mutex_unlock(&sessions[client].mutex);
}
/* ==================================================================================================================== */
/* ==================================================================================================================== */
/* ==================================================================================================================== */

/* ==================================================================================================================== */
/* =============================================== INTERFACCIA CON OBJECT ============================================== */
/* ==================================================================================================================== */
 void take_object(int client_socket, char *target)
{
    int room, client;
    Object *obj;

    client = find_session(client_socket);
    if (client < 0 || client > MAX_CLIENTS || !sessions[client].active || !sessions[client].registered || !sessions[client].playing)
    {
        send_response(client_socket, "take_object: indice client non valido");
        return;
    }

    room = sessions[client].room;

    if (target == NULL)
    {
        send_response(client_socket, "take_object: target non valido\n");
        return;
    }

    if (sessions[client].nitems == CARRY_CAPACITY)
    {
        send_response(client_socket, "take_object: inventario pieno\n");
        return;
    }


    obj = remove_object(room, target);
    if (obj == NULL)
    {
        send_response(client_socket, "take_object: %s non esiste\n", target);
        return;
    }

    sessions[client].inventory[sessions[client].nitems++] = obj;
}
/* ==================================================================================================================== */
/* ==================================================================================================================== */
/* ==================================================================================================================== */
