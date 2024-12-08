#ifndef IMPLEMENTATION_H
#define IMPLEMENTATION_H

void init_data();

/* AUTENTICAZIONE */
void handle_signup(int, char[FIELD_SIZE], char[FIELD_SIZE]);
void handle_login(int, char[FIELD_SIZE], char[FIELD_SIZE]);
void handle_logout(int);

/* GIOCO */
void handle_start(int, int);
void handle_look(int, char*);
void handle_take(int, char*);
void handle_objs(int);
void handle_use(int, char*, char*);
void handle_end(int);

/* INTERFACCIA CLIENT */
void create_client(int);
void add_client(int, int);
void remove_client(int);

/* INTERFACCIA OBJECT */
void take_object(int, char*);

#endif