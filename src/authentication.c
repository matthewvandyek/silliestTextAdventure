#include "common.h"
#include "structures.h"
#include "authentication.h"

extern Session sessions[MAX_CLIENTS];

/**
 * @note Sia il main thread che i timer delle room possono modificare una Session.
 * Nonostante, i timer non accedono al campo {Session.registered} e se un client
 * non è registrato, non può giocare. Non ci sono problemi di concorrenza per
 * le funzioni [handle_signup] e [handle_login].
 */

/* ==================================== PRINCIPALI ===================================== */
void handle_signup(int client_socket, char username[FIELD_SIZE], char password[FIELD_SIZE])
{
    int client = find_session(client_socket);
    char hashed_password[BUFFER_SIZE];
    FILE *fd;

    /* possibili errori di uso del comando */
    if (sessions[client].registered) /* per mancanza di mutex vedi (AC0) */
    {
        send_response(client_socket, "signup: un utente e' gia' attivo su questo socket\n");
        return;
    }

    if ((fd = fopen("users.txt", "a")) == NULL)
    {
        send_response(client_socket, "signup: errore nell'apertura del file\n");
        return;
    }

    hash(password, hashed_password);

    /* check if user already exists */
    if (find_user(username) != -1)
    {
        send_response(client_socket, "signup: l'utente %s esiste gia'", username);
        fclose(fd);
        return;
    }

    /* otherwise salvalo nel file */
    fprintf(fd, "%s %s\n", username, hashed_password);

    /* fine! */
    send_response(client_socket, "signup: utente %s registrato", username);
    fclose(fd);
}

void handle_login(int client_socket, char username[FIELD_SIZE], char password[FIELD_SIZE])
{
    int client = find_session(client_socket);
    FILE *fd;
    char line[BUFFER_SIZE], found_password[BUFFER_SIZE], hashed_password[BUFFER_SIZE];
    long position;

    if (sessions[client].registered) /* per mancanza di mutex vedi (AC0) */
    {
        send_response(client_socket, "login: c'e un'altro user registrato su questo socket");
        return;
    } 

    if ((fd = fopen("users.txt", "r")) == NULL)
    {
        send_response(client_socket, "login: errore nell'apertura del file, prova a fare signup");
        return;
    }

    hash(password, hashed_password);

    if ((position = find_user(username)) == -1)
    {
        send_response(client_socket, "login: l'utente %s non esiste", username);
        fclose(fd);
        return;
    }
    
    /* metti cursore alla posizione dell'utente trovato */
    fseek(fd, position, SEEK_SET);
    
    /* leggi la linea */
    if (fgets(line, sizeof line, fd) == NULL)
    {
        send_response(client_socket, "login: errore nella lettura del file");
        fclose(fd);
        return;
    }

    /* estrai hashed pw e vedi se coincidono */
    sscanf(line, "%*s %s", found_password);

    /* SE SUCCESS */
    if (strcmp(hashed_password, found_password) == 0)
    {
        /* buono, aggiorna le strutture dati */
        sessions[client].registered = 1;
        strcpy(sessions[client].username, username);

        /* fine! */
        send_response(client_socket, "utente %s loggato con successo", username);
        
        fclose(fd);
        return;
    }
    
    /* SE FAILURE */
    send_response(client_socket, "login: password errata per utente %s", username);
    fclose(fd);
}

void handle_logout(int client_socket)
{
    int client = find_session(client_socket);

    if (!sessions[client].active) /* per mancanza di mutex vedi (AC0) */
    {
        send_response(client_socket, "logout: nessun utente attivo, niente da fare!");
        return;
    }

    /* cosa succede se l'utente sta giocando? */
    if (sessions[client].playing)
    {
        send_response(client_socket, "logout: chiudi la partita prima di fare logout\n");
        return;
    }

    sessions[client].registered = 0;
    send_response(client_socket, "logout: utente %s logged out con successo", sessions[client].username);

}
/* ===================================================================================== */


/* ===================================== SUPPORTO ====================================== */
void hash(char *input, char *output)
{
    /**
    * @brief Algoritmo DJB2: semplice, efficiente e buona distribuzione.
    */
    unsigned long hash = 5381;  /* nr magico scelto da Bernstein */ 
    int c;                      /* per una buona distribuzione */ 

    /* fa hash*33 + c per ogni c in input */
    while ((c = *input++)) hash = ((hash << 5) + hash) + c;

    sprintf(output, "%lu", hash);
}

void delete_user(const char *username)
{
    /* questa va rivista se la vuoi usare */

    FILE *fd, *temp_fd;
    char line[BUFFER_SIZE], found_username[FIELD_SIZE];
    int client;

    if ((fd = fopen("users.txt", "r")) == NULL || (temp_fd = fopen("temp_users.txt", "w")) == NULL)
    {
        printf("delete_user: errore nell'apertura del file\n");
        return;
    }

    /* ma se il user ha gia fatto login su qualche socket? 
     * preferisco non rimuoverlo perche onestamente meno sbatti
     * e non vedo qual'e il benefit a dover cancellarlo per forza per adesso
     */
    
    client = find_session_by_username(username);

    if (client != -1 && sessions[client].active && sessions[client].registered)
    {
        printf("delete_user: il client %s e connesso al server sul socket %d, aspetta che si scolleghi\n", username, sessions[client].sockfd);
        return;
    }

    while (fgets(line, sizeof line, fd))
    {
        sscanf(line, "%s", found_username);
        if (strcmp(username, found_username) != 0) fputs(line, temp_fd);
    }

    fclose(fd); fclose(temp_fd);

    remove("users.txt"); rename("temp_users.txt", "users.txt");
}

long find_user(const char* username)
{
    /* funzione privata, non manda risposta al client */
    FILE *fd;
    char line[BUFFER_SIZE];
    char found_username[FIELD_SIZE];
    char found_password[FIELD_SIZE];
    long position;

    if ((fd = fopen("users.txt", "r+")) == NULL)
    {
        printf("find_user: errore nell'apertura del file\n");
        return -1;
    }

    while (fgets(line, sizeof(line), fd) != NULL)
    {
        position = ftell(fd);
        sscanf(line, "%s %s", found_username, found_password);
        if (strcmp(username, found_username) == 0)
        {
            fclose(fd);
            return position - strlen(line); /* nr riga */
        }
    }
    fclose(fd);
    return -1;

}
/* ===================================================================================== */


