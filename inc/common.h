#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>

#include <netinet/in.h>
#include <arpa/inet.h> 
#include <sys/socket.h>
#include <sys/select.h>

#define BUFFER_SIZE 1024
#define FIELD_SIZE 64
#define DATA_STRUCTS 6
#define MAX_ERROR_CODES 10
#define EOM "\r\n\r\n"
#define EOM_SIZE 4
#define EOG "\r0\n0\r0\n"
#define EOG_SIZE 7
#define FREE_SLOT -1
#define MAX_CLIENTS 10
#define MAX_ROOMS 5
#define CARRY_CAPACITY 10


/* ==================================================================================================================== */
/* ===================================================== TCP UTILS ==================================================== */
/* ==================================================================================================================== */
void append_message(int, const char *, ...);
void send_message(int, const char*);
void send_response(int, const char*, ...);
void send_response_exclusive(int, const char*, ...);
ssize_t recv_message(int, char **);
/* ==================================================================================================================== */
/* ==================================================================================================================== */
/* ==================================================================================================================== */



/* ==================================================================================================================== */
/* =================================================== ERROR MODULE =================================================== */
/* ==================================================================================================================== */
typedef void (*ErrorHandler)(void*);

typedef enum EObject { COMMON, SERVER, SESSION } ErrorObject;
typedef enum ECode { NADA, PARAM, IO, UNKNOWN, WRONG_USAGE, SYS } ErrorCode;

typedef struct { int n_codes; ErrorHandler* handlers; int* codes; } Error;

/* PRIVATE - USATE DA ALTRE FUNZIONI NEL MODULO */
int find_error_index(ErrorObject, ErrorCode);

/* PUBBLICHE - USATE DA FUNZIONI IN ALTRI MODULI O FILE */
void init_errors(); 
void handle_error(ErrorObject, ErrorCode, char*, char*);
/* ==================================================================================================================== */
/* ==================================================================================================================== */
/* ==================================================================================================================== */

void clear_screen();

#endif