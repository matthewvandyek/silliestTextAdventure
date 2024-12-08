#ifndef AUTHETICATION_H
#define AUTHETICATION_H

/* ==================== INTERFACCIA ==================== */
void handle_signup(int, char[FIELD_SIZE], char[FIELD_SIZE]);
void handle_login(int, char[FIELD_SIZE], char[FIELD_SIZE]);
void handle_logout(int);

/* ===================== SUPPORTO ====================== */
void hash(char*, char*);
long find_user(const char*);
void delete_user(const char*); /* made on a whim */

#endif