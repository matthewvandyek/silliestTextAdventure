#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define EOM "\r\n\r\n"
#define EOM_SIZE 4
#define EOG "\r0\n0\r0\n"
#define EOG_SIZE 7
#define FREE -1

#define SERVER_PORT 4242

typedef struct
{
    int sockfd;
    int port;
    int room, ttl, ntokens, nsolved;
} Session;

Session session;

void init_session(int, char**);
void start_session();

void menu();
void send_command(const char*);
void recv_response(char**);
void print_menu();

int main(int argc, char** argv)
{
    init_session(argc, argv);

    start_session();

    while (1) menu();

    return 0;
}

void start_session()
{
    int opt = 1;
    struct sockaddr_in client_addr;
    struct sockaddr_in server_addr;

    if ((session.sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    if (setsockopt(session.sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        exit(1);
    }

    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(session.port);
    client_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(session.sockfd, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0)
    {
        perror("bind");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (connect(session.sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect");
        exit(1);
    }

    printf("connesso al server sulla porta %d\n", session.port); 
}

void init_session(int argc, char **argv)
{
    if (argc > 2)
    {
        printf("uso: ./%s <porta>\n", argv[0]);
        exit(1);
    }

    session.port = (argc == 2) ? atoi(argv[1]) : 6000;
    session.sockfd = FREE;
    session.room = FREE;

    printf("init -> initializing server on port %d\n", session.port);
}

ssize_t recv_message(char **message)
{
    char buffer[BUFFER_SIZE];
    ssize_t nbytes, tbytes;

    *message = NULL;
    tbytes = 0;

    while ((nbytes = recv(session.sockfd, buffer, sizeof buffer, 0)) > 0)
    {
        *message = realloc(*message, tbytes + nbytes + 1);
        memcpy(*message + tbytes, buffer, nbytes);
        tbytes += nbytes;
        (*message)[tbytes] = '\0';

        if (tbytes > EOM_SIZE && memcmp(*message + tbytes - EOM_SIZE, EOM, EOM_SIZE) == 0)
        {
            *message = realloc(*message, tbytes - EOM_SIZE + 1);
            (*message)[tbytes - EOM_SIZE + 1] = '\0';
            return tbytes;
        }
    }

    return tbytes;
}

void send_message(const char *input_message)
{
    char *message = NULL;
    ssize_t tbytes, nbytes;
    ssize_t message_len, input_message_len;

    fflush(stdout);

    input_message_len = strlen(input_message);
    message_len = input_message_len + EOM_SIZE;

    message = (char *)malloc(message_len + 1);
    if (message == NULL)
    {
        perror("malloc");
        return;
    }

    memcpy(message, input_message, input_message_len);
    memcpy(message + input_message_len, EOM, EOM_SIZE);
    message[message_len] = '\0';
    tbytes = 0; 

    while (tbytes < message_len) 
    {
        nbytes = send(session.sockfd, message + tbytes, message_len - tbytes, 0);
        if (nbytes <= 0) { perror("send"); break; }
        tbytes += nbytes;
    }

    free(message);
}

void handle_response(const int recv_bytes, char** response, char* input)
{
    char* eog_pointer = NULL;

    if (recv_bytes == 0)
    {
        printf("menu -> handle_response -> connection closed by server\n");
        free(*response);
        exit(0);
    }
    else if (recv_bytes < 0)
    {
        perror("recv_message");
        free(*response);
        exit(1);
    }

    /* else recv_bytes > 0 */
    /* get info on token raccolti, token mancanti, token rimanenti */

    /* se c'e EOG lo rimuoviamo e reimpostiamo valori della sessione */
    if ((eog_pointer = strstr(*response, EOG)) != NULL)
    {
        /* params: dest pointer, src pointer, number of bytes from >>>>[after]<<<< EOG to end of string */
        memmove(eog_pointer, eog_pointer + EOG_SIZE, strlen(eog_pointer + EOG_SIZE) + 1);

        /* riazzera sessione */
        session.room = FREE;
        session.ttl = FREE;
        session.ntokens = FREE;
        session.nsolved = FREE;
    }

    printf("server: %s\n", *response);
}

void menu()
{
    char *input = malloc(BUFFER_SIZE);
    char *response = NULL;
    ssize_t recv_bytes;

    if (input == NULL)
    {
        fprintf(stderr, "menu -> memory allocation failed\n");
        return;
    }

    printf(">| ");

    if (fgets(input, BUFFER_SIZE, stdin) == NULL)
    {
        fprintf(stderr, "menu -> error reading input\n");
        free(input);
        return;
    }

    input[strcspn(input, "\n")] = '\0';
    send_message(input);

    recv_bytes = recv_message(&response);
    handle_response(recv_bytes, &response, input);

    free(input);
    free(response);
}

void print_menu()
{
    #ifndef _WIN32
        system("clear");
    #else
        system("cls");
    #endif

    printf("***************************** MENU *****************************\n");
    printf("Comandi disponibili:\n");
    printf("1. login <username> <password>  - Effettua il login\n");
    printf("2. signup <username> <password> - Effettua la registrazione\n");
    printf("3. start <room_id>              - Inizia il gioco nella stanza specificata\n");
    printf("4. look [object_name]           - Osserva la stanza o un oggetto specifico\n");
    printf("5. take <object_name>           - Raccogli un oggetto\n");
    printf("6. use <object1> [object2]      - Usa un oggetto o combina due oggetti\n");
    printf("7. objs                         - Mostra gli oggetti raccolti\n");
    printf("8. end                          - Termina il gioco e chiude la connessione\n");
    printf("****************************************************************\n");

    if (session.room == -1)
    {
        /* ricevi e stampa le rooms */
    }
    else
    {
        /* stampa: */
        /* token raccolti */
        /* token mancanti */
        /* tempo rimanente */
    }
}