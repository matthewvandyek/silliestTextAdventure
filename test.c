#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_GAMES 3
#define BUFFER_SIZE 256
#define FREE_SLOT -1
#define NOT_FOUND -1

void menu();
void print_menu();
void log_response(const char*);
void clear_screen();

char prev_response[BUFFER_SIZE] = "welcome to the game";

typedef struct 
{
    pthread_t thread_id;
    int ttl, stop_timer;
    int running;
    int code;
} Game;
void init_games();
void init_game(int);
int find_game(int);
int load_game(int);
void start_game(int);
void stop_game(int);
void destroy_game(int);

Game games[MAX_GAMES];
int ngames;

void *timer_thread(void *arg)
{
    Game *game = (Game*)arg;

    /* save cursor position */
    printf("\033[s");

    while (game->ttl > 0)
    {
        if (game->stop_timer)
        {
            log_response("timer stopped\n");
            return NULL;
        }

        printf("\033[4;0H");
        printf("\033[K");
        printf("%d seconds left", game->ttl);
        fflush(stdout);

        printf("\033[u");
        fflush(stdout);

        sleep(1);
        game->ttl--;
    }

    printf("\033[4;0H");
    printf("\033[K");
    printf("time's up");
    fflush(stdout);

    printf("\033[u");
    fflush(stdout);


    game->running = 0;

    int g = find_game(game->code);
    destroy_game(g);
    
    return NULL;
}

int main()
{
    init_games();
    
    while (1) menu();

    return EXIT_SUCCESS;
}

/* game methods */

void init_game(int g)
{
    games[g].code = FREE_SLOT;
    games[g].stop_timer = 0;
    games[g].ttl = 0;
    games[g].running = 0;
}

void init_games()
{
    for (int g = 0; g < MAX_GAMES; g++) init_game(g);
    ngames = 0;
}

int find_game(int game_code)
{
    int g;
    char response[BUFFER_SIZE];

    for (g = 0; g < MAX_GAMES; g++)
    {
        if (games[g].code == game_code)
        {
            snprintf(response, BUFFER_SIZE, "found game with code %d at index %d", game_code, g);
            log_response(response);
            return g;
        }
    }

    snprintf(response, BUFFER_SIZE, "could not find any loaded game with code %d", game_code);
    log_response(response);
    return NOT_FOUND;
}

int load_game(int game_code)
{
    if (game_code != 0)
    {
        log_response("load: game code doesn't exist\n");
        return NOT_FOUND;
    }

    if (ngames >= MAX_GAMES-1)
    {
        log_response("load: all slots to save games are occupied\n");
        return NOT_FOUND;
    }

    Game* game = &games[ngames];
    if (game->code != FREE_SLOT)
    {
        log_response("load: game is already loaded\n");
        return -1;
    }

    game->code = game_code;
    game->ttl = 5;

    return ngames++;
}

void start_game(int game_code)
{
    int g;
    Game *found;

    if (game_code != 0)
    {
        log_response("start: game code doesn't exist\n");
        return;
    }

    if (ngames >= MAX_GAMES-1)
    {
        log_response("start: all slots to save games are occupied\n");
        return;
    }

    if (g != NOT_FOUND && games[g].running)
    {
        log_response("start: game is already running\n");
        return;
    }

    g = find_game(game_code);
    
    if (g != NOT_FOUND)
    {
        games[g].running = 1;
        return;
    }

    /* game not loaded */
    g = load_game(game_code);

    if (pthread_create(&(games[g].thread_id), NULL, timer_thread, &games[g]) != 0)
    {
        log_response("load: failed to create timer thread");
        return;
    }

    pthread_detach(games[g].thread_id);

    games[g].running = 1;
}

void stop_game(int g)
{
    if (g < 0 || g >= MAX_GAMES)
    {
        log_response("stop: game slot doesn't exist\n");
        return;
    }

    if (games[g].code == FREE_SLOT)
    {
        log_response("stop: nothing to stop\n");
        return;
    }

    /* signal the thread to stop */
    games[g].stop_timer = 1;

    // if (pthread_join(games[g].thread_id, NULL) != 0)
    // {
    //     log_response("stop: failed to join timer thread");
    //     return;
    // }

    games[g].running = 0;
}

void destroy_game(int g)
{
    if (g < 0 || g >= MAX_GAMES)
    {
        log_response("destroy: game slot doesn't exist\n");
        return;
    }

    if (games[g].code == FREE_SLOT)
    {
        log_response("destroy: game slot is already free\n");
        return;
    }
    else stop_game(g);
    
    init_game(g);

    /* shift back games to keep things clean */
    if (g < ngames-1) 
        for (int i = g; i < ngames-1; i++) 
            games[i] = games[i+1];

    ngames--;
}

void menu()
{
    char command[BUFFER_SIZE] = {0};
    int game_code = -1;

    print_menu();

    printf(">| ");
    if (fgets(command, BUFFER_SIZE, stdin) == NULL)
    {
        log_response("menu: failed to read command");
        return;
    }

    if (sscanf(command, "start %d", &game_code) == 1) start_game(game_code);
    else if (sscanf(command, "stop %d", &game_code) == 1) destroy_game(game_code);
    else log_response("menu: invalid command");
}

void print_menu()
{
    clear_screen();

    printf("start <game_code> - load and start game with given code\n");
    printf("stop <game_code> - stop game with given code\n");
    
    printf("\n");
    if (prev_response[0] != 0) printf("%s\n\n", prev_response);
}

/* utilities */

void log_response(const char* message)
{
    snprintf(prev_response, BUFFER_SIZE, "%s", message);
}

void clear_screen()
{
    printf("\033[H\033[J");
}