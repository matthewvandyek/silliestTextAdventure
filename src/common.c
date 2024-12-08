#include "common.h"



/* ==================================================================================================================== */
/* ===================================================== TCP UTILS ==================================================== */
/* ==================================================================================================================== */
void append_message(int client_socket, const char *format, ...)
{
    va_list args;
    ssize_t tbytes, nbytes;
    char buffer[BUFFER_SIZE];
    int message_len;

    va_start(args, format);
    vsprintf(buffer, format, args);
    va_end(args);

    message_len = strlen(buffer);
    tbytes = 0; 

    while (tbytes < message_len)
    {
        if ((nbytes = send(client_socket, buffer + tbytes, message_len - tbytes, 0)) <= 0)
        {
            perror("send");
            break;
        }
        tbytes += nbytes;
    }
}

void send_message(int client_socket, const char *input_message)
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
        nbytes = send(client_socket, message + tbytes, message_len - tbytes, 0);
        if (nbytes <= 0) { perror("send"); break; }
        tbytes += nbytes;
    }

    free(message);
}

void send_response(int client_socket, const char *format, ...)
{
    char buffer[BUFFER_SIZE*10]; 
    va_list args;
    
    va_start(args, format);
    vsprintf(buffer, format, args);
    va_end(args);

    printf("%s\n", buffer);
    send_message(client_socket, buffer);
}

void send_response_exclusive(int client_socket, const char *format, ...)
{
    /* esattamente la stessa cosa ma non printa per il server 
        si lo so che fa schifo come soluzione */
    char buffer[BUFFER_SIZE*10]; 
    va_list args;
    
    va_start(args, format);
    vsprintf(buffer, format, args);
    va_end(args);

    send_message(client_socket, buffer);
}

ssize_t recv_message(int client_socket, char **message)
{
    char buffer[BUFFER_SIZE];
    ssize_t nbytes, tbytes;

    *message = NULL;
    tbytes = 0;

    while ((nbytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        char *new_message = realloc(*message, tbytes + nbytes + 1);
        if (new_message == NULL)
        {
            perror("realloc");
            free(*message);
            *message = NULL;
            return -1;
        }


        *message = new_message;
        memcpy(*message + tbytes, buffer, nbytes);
        tbytes += nbytes;
        (*message)[tbytes] = '\0';

        if (tbytes >= EOM_SIZE && memcmp(*message + tbytes - EOM_SIZE, EOM, EOM_SIZE) == 0)
        {
            (*message)[tbytes - EOM_SIZE] = '\0';
            return tbytes - EOM_SIZE;
        }
    }

    /* se sono qui vuol dire che non ho ricevuto EOM */

    /** siccome rimuovo EOM dal messaggio, per capire quando la connessione e stata
     * chiusa dal cliente -- di solito con SIGINT -- devo ritornare qualcosa di
     * diverso per far discernere un messaggio vuoto da una chiusura del processo client
    **/

    if (nbytes == 0)
    {
        return -1;
    }

    if (nbytes < 0)
    {
        perror("recv");
        free(*message);
        *message = NULL;
        return -2;
    }

    return tbytes;
}
/* ==================================================================================================================== */
/* ==================================================================================================================== */
/* ==================================================================================================================== */


/* ==================================================================================================================== */
/* =================================================== ERROR MODULE =================================================== */
/* ==================================================================================================================== */
/* OGGETTI */
Error errors[DATA_STRUCTS];

/* HANDLERS */
void handle_param_error(void *message)
{
    if (message) printf(" -> %s -> exit(1)", (char *)message);
    else printf(" -> handle_param_error -> exit(1)");
    
    printf("\n"); /* serve perche termino il processo */
    exit(1);
}

void handle_io_error(void)
{
    perror("fgets");
}

void handle_unknown(void *message)
{
    size_t len;

    if (!message)
    {
        /* comportamento di default */
        printf(" -> comando sconosciuto");
        return;
    }
    
    if ((len = strlen((char *)message)) > 0 && ((char *)message)[len - 1] == '\n') ((char *)message)[len-1] = '\0';

    if (((char *)message)[0] == '\0') printf(" -> comando vuoto");
    else printf(" -> comando %s non riconosciuto", (char *)message);
}

void handle_wrong_usage(void *message)
{
    int len;

    if (!message)
    {   /* comportamento di default */
        printf(" -> uso scorretto");
        return;
    }

    len = strlen((char *)message);
    if (len > 0 && ((char *)message)[len - 1] == '\n')
    {
        ((char *)message)[len - 1] = '\0';
        len--;
    }

    if (len == 0) printf(" -> uso scorretto");
    else printf(" -> uso: %s", (char *)message);
}

void handle_system_failure(void *source)
{
    const char *error_message = strerror(errno);
    if (source) printf(" -> %s -> errno: %s", (char *)source, error_message);
    else printf(" -> %s", error_message);

    printf("\n");
    exit(EXIT_FAILURE);
}

/* PRIVATE */
int find_error_index(ErrorObject obj, ErrorCode code)
{
    /* FUNZIONE INTERNA = LA CHIAMO DA UNA FUNZIONE GIA SANITIZZATA */

    int index = -1;

    for (index = 0; index < errors[obj].n_codes; index++)
    {
        if (errors[obj].codes[index] == (int)code) return index;
    }

    return -1;
}

/* PUBBLICHE */
void init_errors()
{
    /* ======================================= COMMON ======================================= */
    /* INIT MEMORIA */
    errors[COMMON].n_codes = 1;
    errors[COMMON].handlers = (ErrorHandler*)malloc(errors[COMMON].n_codes * sizeof(ErrorHandler));
    errors[COMMON].codes = (int*)malloc(errors[COMMON].n_codes * sizeof(int));

    /* ASSEGNAZIONE */
    errors[COMMON].codes[0] = PARAM;
    errors[COMMON].handlers[0] = handle_param_error;
    /* ====================================================================================== */


    /* ======================================= SERVER ======================================= */
    /* INIT MEMORIA */
    errors[SERVER].n_codes = 3;
    errors[SERVER].handlers = (ErrorHandler*)malloc(errors[SERVER].n_codes * sizeof(ErrorHandler));
    errors[SERVER].codes = (int*)malloc(errors[SERVER].n_codes * sizeof(int));

    /* ASSEGNAZIONE */
    errors[SERVER].codes[0] = UNKNOWN;
    errors[SERVER].handlers[0] = handle_unknown;
    errors[SERVER].codes[1] = WRONG_USAGE;
    errors[SERVER].handlers[1] = handle_wrong_usage;
    errors[SERVER].codes[2] = SYS;
    errors[SERVER].handlers[2] = handle_system_failure;
    /* ====================================================================================== */
}

void handle_error(ErrorObject obj, ErrorCode code, char* caller, char* message)
{
    int index = -1;

    /* SANITIZZAZIONE DELL'INPUT */
    if (obj < 0 || obj >= DATA_STRUCTS)
    {
        printf("Error: handle_error -> obj %d out of range\n", obj);
        return;
    }
    if (code < 0 || code >= MAX_ERROR_CODES)
    {
        printf("Error: %s -> handle_error -> code %d out of range\n", caller, code);
        return;
    }
    if (errors[obj].n_codes == 0)
    {
        printf("Error: %s -> handle_error -> no error codes defined for data structure %d\n", caller, obj);
        return;
    }
    if (caller == NULL)
    {
        printf("Error: ? -> handle_error -> caller is NULL (very mysterious...)\n");
        return;
    }

    /* FORSE QUI SIAMO SALVI */
    index = find_error_index(obj, code);

    printf("%s", caller);

    if (index != -1 && errors[obj].handlers[index])
    {
        if (message) errors[obj].handlers[index](message);
        else errors[obj].handlers[index](NULL);
    }
    else if (message) printf(" -> %s", message);

    printf("\n");
}
/* ==================================================================================================================== */
/* ==================================================================================================================== */
/* ==================================================================================================================== */



void clear_screen()
{
    #ifndef _WIN32
        system("clear");
    #else
        system("cls");
    #endif
}