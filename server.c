#include "common.h"
#include "implementation.h"

/* CLASS */
typedef struct
{
    int running, server_socket, port, max_fd;
    fd_set master_fds;
} Server;

/* INSTANCE */
Server server;

/* METHODS */
void init_server(int, char**);
void accept_server();
void respond_server(int);
void multiplex_server();
void start_server(int);
void stop_server();

void init(int argc, char **argv);
void menu();
void serve(int, const char[BUFFER_SIZE]);

int main(int argc, char** argv)
{
    init(argc, argv);

    /* per far partire il script */
    start_server(-1);

    while (1) menu();

    return 0;
}

void accept_server()
{
    int client_socket;
    struct sockaddr_in client_addr;
    socklen_t client_addrlen = sizeof(client_addr);

    client_socket = accept(server.server_socket, (struct sockaddr*)&client_addr, &client_addrlen);
    if (client_socket < 0) handle_error(SERVER, SYS, "accept_server", "accept");

    FD_SET(client_socket, &server.master_fds);
    if (client_socket > server.max_fd) server.max_fd = client_socket;

    create_client(client_socket);

    printf("accept_server -> nuova connessione da %s sul socket %d\n", inet_ntoa(client_addr.sin_addr), client_socket);
}

void respond_server(int client_socket)
{
    char *response;
    ssize_t nbytes;

    response = NULL;
    nbytes = recv_message(client_socket, &response);

    if (nbytes == -2)
    {
        handle_error(SERVER, SYS, "respond_server", "recv");
        close(client_socket);
        FD_CLR(client_socket, &server.master_fds);
    }
    else if (nbytes == -1)
    {
        printf("client %d ha chiuso la connessione\n", client_socket);
        remove_client(client_socket);
        close(client_socket);
        FD_CLR(client_socket, &server.master_fds);
    }
    else
    {
        int n;
        for (n = nbytes - 1; n >= 0; n--)
        {
            if (response[n] == '\n' || response[n] == '\r') response[n] = '\0';
            else break;
        }

        printf("client %d: %s\n", client_socket, response);
        serve(client_socket, response);
    }

    free(response);
    response = NULL;
}

void multiplex_server()
{
    printf("multiplex_server -> server in attesa di connessioni\n");
    while (1)
    {
        int fd; fd_set read_fds = server.master_fds;
        
        if (select(server.max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) handle_error(SERVER, SYS, "multiplex_server", "select");

        for (fd = 0; fd <= server.max_fd; fd++) if (FD_ISSET(fd, &read_fds))
        {
            if (fd == server.server_socket) accept_server();
            else if (fd == STDIN_FILENO) menu();
            else respond_server(fd);
        }
    }
    printf("multiplex_server -> terminato\n");
}

void start_server(int new_port)
{
    struct sockaddr_in server_addr;
    int opt = 1;
    
    if (server.running == 1)
    {
        handle_error(SERVER, NADA, "start_server", "server gia avviato, niente da fare");
        return;
    }

    if (new_port >= 0) server.port = new_port;

    if ((server.server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) handle_error(SERVER, SYS, "start_server", "socket");
    if (setsockopt(server.server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) handle_error(SERVER, SYS, "start_server", "setsockopt");
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server.port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server.server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) handle_error(SERVER, SYS, "start_server", "bind");
    if (listen(server.server_socket, -1) < 0) handle_error(SERVER, SYS, "start_server", "listen");

    FD_ZERO(&server.master_fds);
    FD_SET(server.server_socket, &server.master_fds);
    FD_SET(STDIN_FILENO, &server.master_fds);
    server.max_fd = server.server_socket;

    server.running = 1;
    printf("start_server -> server partito sulla porta %d\n", server.port);
    
    multiplex_server();
}

void stop_server()
{
    int fd;
    if (server.running == 0)
    {
        handle_error(SERVER, NADA, "stop_server", "server gia stoppato, niente da fare");
        return;
    }

    for (fd = 1; fd < server.max_fd; fd++)
    {
        if (FD_ISSET(fd, &server.master_fds))
        {
            remove_client(fd); /* funzione atomica */
            close(fd); /* syscall e thread-safe */
            FD_CLR(fd, &server.master_fds); /* dati privati server */
        }
    }

    close(server.server_socket);
    FD_CLR(server.server_socket, &server.master_fds);

    server.running = 0;
    printf("stop_server -> server arrestato\n");
}

void init_server(int argc, char **argv)
{
    int port;
    if (argc > 2) handle_error(COMMON, PARAM, "init_server", "too many arguments passed");

    port = (argc == 2) ? atoi(argv[1]) : 4242;
    printf("init_server -> sto inizializzando Server sulla porta %d\n", port);

    server.port = port;
    server.running = 0;
    server.server_socket = -1;
}

void init(int argc, char **argv)
{
    init_errors();
    init_server(argc, argv);
    init_data();
}

void menu()
{
    char cmd[BUFFER_SIZE];
    int check_start, check_stop;

    memset(cmd, 0, BUFFER_SIZE);
    if (fgets(cmd, sizeof cmd, stdin) == NULL)
    {
        handle_error(COMMON, IO, "menu", "fgets");
        return;
    }

    if (cmd[strlen(cmd) - 1] == '\n') cmd[strlen(cmd) - 1] = '\0';

    check_start = (strncmp(cmd, "start", 5) == 0);
    check_stop = (strcmp(cmd, "stop") == 0);
    
    if (check_start)
    {
        int ret, new_port = -1;
        char trash[BUFFER_SIZE] = {0};
        
        ret = sscanf(cmd, "start %d %s", &new_port, trash);
        if (ret == 0 || ret == 2) handle_error(SERVER, WRONG_USAGE, "menu", "start [port]");
        else if (ret == 1 && new_port < 0) handle_error(SERVER, WRONG_USAGE, "menu", "port >= 0");

        else start_server((ret == 1) ? new_port : -1);
    }
    else if (check_stop) stop_server();
    else handle_error(SERVER, UNKNOWN, "menu", cmd);
}

void serve(int client_socket, const char buffer[BUFFER_SIZE])
{
    char command[FIELD_SIZE] = {0};
    char arg1[FIELD_SIZE], arg2[FIELD_SIZE];
    int ret;

    ret = sscanf(buffer, "%s %s %s", command, arg1, arg2);

    /* AUTENTICAZIONE */
    if (strcmp(command, "signup") == 0)
    {
        if (ret == 3) handle_signup(client_socket, arg1, arg2);
        else send_response(client_socket, "uso: signup <username> <password>");
    }
    else if (strcmp(command, "login") == 0)
    {
        if (ret == 3) handle_login(client_socket, arg1, arg2);
        else send_response(client_socket, "uso: login <username> <password>");
    }
    else if (strcmp(command, "logout") == 0)
    {
        if (ret == 1) handle_logout(client_socket);
        else send_response(client_socket, "uso: logout");
    }
    
    /* ACTUAL GIOCO */
    else if (strcmp(command, "start") == 0)
    {
        if (ret == 2) handle_start(client_socket, atoi(arg1));
        else send_response(client_socket, "uso: start <room>");
    }
    else if (strcmp(command, "look") == 0)
    {
        if (ret == 1) handle_look(client_socket, NULL);
        else if (ret == 2) handle_look(client_socket, arg1);
        else send_response(client_socket, "uso: look [location | object]");
    }
    else if (strcmp(command, "take") == 0)
    {
        if (ret == 2) handle_take(client_socket, arg1);
        else send_response(client_socket, "uso: take <object>");
    }
    else if (strcmp(command, "use") == 0)
    {
        if (ret == 2) handle_use(client_socket, arg1, NULL);
        else if (ret == 3) handle_use(client_socket, arg1, arg2);
        else send_response(client_socket, "uso: use <object1> [object2]");
    }
    else if (strcmp(command, "objs") == 0)
    {
        if (ret == 1) handle_objs(client_socket);
        else send_response(client_socket, "uso: objs");
    }
    else if (strcmp(command, "end") == 0)
    {
        if (ret == 1) handle_end(client_socket);
        else send_response(client_socket, "uso: end");
    }
    else send_response(client_socket, "comando non riconosciuto");
}
