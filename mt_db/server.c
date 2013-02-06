#include <assert.h>
/* FreeBSD */
#define _WITH_GETLINE
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "window.h"
#include "db.h"
#include "words.h"
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>

/* the encapsulation of a client thread, i.e., the thread that handles
 * commands from clients */
typedef struct Client {
	pthread_t thread;
	window_t *win;
        volatile bool done;
        struct Client* next;
} client_t;

typedef struct ThreadHandler {
  client_t* clients;
  pthread_mutex_t clients_mutex;
  pthread_t thread;
} thread_handler_t;

/* Interface with a client: get requests, carry them out and report results */
void *client_run(void *);
/* Interface to the db routines.  Pass a command, get a result */
int handle_command(char *, char *, int len);

/*
 * Create an interactive client - one with its own window.  This routine
 * creates the window (which starts the xterm and a process under it.  The
 * window is labelled with the ID passsed in.  On error, a NULL pointer is
 * returned and no process started.  The client data structure returned must be
 * destroyed using client_destroy()
 */
client_t *client_create(int ID) {
    client_t *new_Client = (client_t *) malloc(sizeof(client_t));
    char title[16];
    new_Client->done = false;
    new_Client->next = NULL;

    if (!new_Client) return NULL;

    sprintf(title, "Client %d", ID);

    /* Creates a window and set up a communication channel with it */
    if ((new_Client->win = window_create(title))) return new_Client;
    else {
	free(new_Client);
	return NULL;
    }
}

/*
 * Create a client that reads cmmands from a file and writes output to a file.
 * in and out are the filenames.  If out is NULL then /dev/stdout (the main
 * process's standard output) is used.  On error a NULL pointer is returned.
 * The returned client must be disposed of using client_destroy.
 */
client_t *client_create_no_window(char *in, char *out) {
    char *outf = (out) ? out : "/dev/stdout";
    client_t *new_Client = (client_t *) malloc(sizeof(client_t));
    if (!new_Client) return NULL;

    /* Creates a window and set up a communication channel with it */
    if( (new_Client->win = nowindow_create(in, outf))) return new_Client;
    else {
	free(new_Client);
	return NULL;
    }
}

/*
 * Destroy a client created with either client_create or
 * client_create_no_window.  The cient data structure, the underlying window
 * (if any) and process (if any) are all destroyed and freed, and any open
 * files are closed.  Do not access client after calling this function.
 */
void client_destroy(client_t *client) {
	/* Remove the window */
	window_destroy(client->win);
	free(client);
}

/* Code executed by the client */
void *client_run(void *arg)
{
	client_t *client = (client_t *) arg;

	/* main loop of the client: fetch commands from window, interpret
	 * and handle them, return results to window. */
	char *command = 0;
	size_t clen = 0;
	/* response must be empty for the first call to serve */
	char response[256] = { 0 };

	/* Serve until the other side closes the pipe */
	while (serve(client->win, response, &command, &clen) != -1) {
	    handle_command(command, response, sizeof(response));
	}
	return 0;
}

int handle_command(char *command, char *response, int len) {
    if (command[0] == EOF) {
	strncpy(response, "all done", len - 1);
	return 0;
    }
    interpret_command(command, response, len);
    return 1;
}

void *client_main(void *arg) {
  client_run(arg);
  client_destroy(arg);
}

void create_client(thread_handler_t* thread_handler) {
  static int started = 0;
  client_t *c = NULL;

  c = client_create(started++);

  c->thread = malloc(sizeof(c->thread));
  pthread_create(c->thread, NULL, client_main, c); 
  
  // Add this thread's info to the clients linked list
  // First lock the list
  pthread_mutex_lock(&thread_handler->clients_mutex);
  // Throw it on the beginning of the list
  c->next = thread_handler->clients;
  thread_handler->clients = c;
  // Unlock the clients list
  pthread_mutex_unlock(&thread_handler->clients_mutex);

}

void handle_main_command(char *command, char *response, int len, thread_handler_t* thread_handler) {
  switch (command[0]) {
    case 'e':
      strncpy(response, "creating new interactive client", len);
      create_client(thread_handler);
      break;
    case 'E':
      strncpy(response, "unimplemented", len);
      break;
    case 's':
      strncpy(response, "unimplemented", len);
      break;
    case 'g':
      strncpy(response, "unimplemented", len);
      break;
    case 'w':
      strncpy(response, "unimplemented", len);
      break;
    default:
      strncpy(response, "ill-formed command", len);
      break;
  }
}

void *thread_handler_main(void *arg)
{
  thread_handler_t *data = arg;


}

int main(int argc, char *argv[]) {
    client_t *c = NULL;	    /* A client to serve */
    int started = 0;	    /* Number of clients started */
    char command[256] = { '\0' };
    char response[256] = { '\0' };
    thread_handler_t thread_handler;
    //Initialize thread_handler
    thread_handler.clients = NULL;
    pthread_mutex_init(&thread_handler.clients_mutex, NULL);

    if (argc != 1) {
	fprintf(stderr, "Usage: server\n");
	exit(1);
    }

    // Launch the thread handler
    pthread_create(&thread_handler.thread, NULL, thread_handler_main, &thread_handler);


    for (;;) {
      fprintf(stdout, ">>> ");
      fgets(command, sizeof(command), stdin);
      handle_main_command(command, response, sizeof(response), &thread_handler);
      fprintf(stdout, "%s\n", response);
    }

    if ((c = client_create(started++)) )  {
	client_run(c);
	client_destroy(c);
    }
    fprintf(stderr, "Terminating.");
    /* Clean up the window data */
    window_cleanup();
    return 0;
}
