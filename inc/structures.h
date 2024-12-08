#ifndef STRUCTURES_H
#define STRUCTURES_H


/* ========================== GAME ============================ */
typedef void (*Effect)(int, void*);

typedef struct Enigma Enigma;
typedef struct Object Object;
typedef struct Location Location;
typedef struct Session Session;
typedef struct EscapeRoom EscapeRoom;

typedef enum 
{
    MULTICHOICE,    /* domanda a risposta multipla */
    RIDDLE,         /* domanda con input risposta */
    COMBO,          /* uso di due oggetti insieme */
    ORDER           /* uso di oggetti singoli in ordine */
} EnigmaType;

typedef union 
{
    struct 
    {
        char question[BUFFER_SIZE];
        char **options;
        int noptions;
        int correct_option;
    } multichoice;

    struct 
    {
        char question[BUFFER_SIZE];
        char answer[BUFFER_SIZE];
    } riddle;

    struct
    {
        /* oggetto da usare insieme */
        Object *other;
    } combo;

    struct 
    {
        Object **objects;
        int nitems;
    } order;

} EnigmaData;

typedef enum
{
    FREE,       /* oggetto che si puo raccogliere */
    BLOCKED,    /* oggetto che necessita la risoluzione di un enigma */
    HIDDEN      /* oggetto non accessibile */
} ObjectState;

struct Enigma
{
    EnigmaType type;
    EnigmaData data;
    int bonus_time; 
};
void destroy_enigma(Enigma*);
int solve_enigma(int, Enigma*);

struct Object
{
    char name[FIELD_SIZE];
    char description[BUFFER_SIZE];

    ObjectState state;

    Enigma *enigma;
    Effect effect;
    void *affected;
};
void destroy_object(Object*);
Object *find_object(int, char*);
Object *find_object_in_inventory(int, char*);

struct Location
{
    char name[FIELD_SIZE];
    char description[BUFFER_SIZE];
};
Location *find_location(int, char*);
void destroy_location(Location*);

struct EscapeRoom
{
    char name[FIELD_SIZE];
    char story[BUFFER_SIZE];
    char description[BUFFER_SIZE];
    
    int nclients;
    int ntokens, nsolved;
    
    int ttl, stop_timer;
    pthread_t timer_thread;

    Location **locations;
    int nlocs;

    Object **objects;
    int nobjs;

    pthread_mutex_t mutex;
};
void* timer(void*);
void init_rooms();
void init_room(EscapeRoom*);
void create_room(int);
void destroy_room(int);
void prolong_room_time(int);
Object *remove_object(int, const char*);
EscapeRoom create_room_0();
/* ============================================================ */


/* ========================== SESSION ========================= */
struct Session
{
    int sockfd;
    char username[FIELD_SIZE];
    
    int active, registered, playing;
    int room;

    Object *inventory[CARRY_CAPACITY];
    int nitems;

    pthread_mutex_t mutex;
};
void init_sessions();
void create_session(int);
void kickout_session(int);
void destroy_session(int); /* prende INDEX client, NOT socket */
int find_session(int);
int find_session_by_username(const char*);
/* ============================================================ */


#endif