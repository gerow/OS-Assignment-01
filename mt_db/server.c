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
typedef struct Client client_t;
typedef struct ThreadHandler thread_handler_t;

struct Client {
  int id;
  pthread_t thread;
  window_t *win;
  volatile bool done;
  thread_handler_t* thread_handler;
  struct Client* next;
};

struct ThreadHandler {
  client_t* clients;
  pthread_mutex_t clients_mutex;
  pthread_t thread;
  pthread_cond_t thread_done_cond;
  bool pause;
  pthread_cond_t pause_cond;
  pthread_mutex_t pause_mutex;
  pthread_cond_t threads_done_cond;
};

/* Interface with a client: get requests, carry them out and report results */
void *client_run(void *);
/* Interface to the db routines.  Pass a command, get a result */
int handle_command(char *, char *, int len);

//Global thread handler (just to make things simpler)
thread_handler_t g_thread_handler;

/*
 * Create an interactive client - one with its own window.  This routine
 * creates the window (which starts the xterm and a process under it.  The
 * window is labelled with the ID passsed in.  On error, a NULL pointer is
 * returned and no process started.  The client data structure returned must be
 * destroyed using client_destroy()
 */
client_t *client_create(int ID) 
{
  client_t *new_Client = (client_t *) malloc(sizeof(client_t));
  char title[16];
  new_Client->done = false;
  new_Client->next = NULL;
  new_Client->thread_handler = &g_thread_handler;
  new_Client->id = ID;

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
client_t *client_create_no_window(char *in, char *out) 
{
  char *outf = (out) ? out : "/dev/stdout";
  client_t *new_Client = (client_t *) malloc(sizeof(client_t));
  if (!new_Client) return NULL;

  new_Client->done = false;
  new_Client->next = NULL;
  new_Client->thread_handler = &g_thread_handler;
  new_Client->id = -1;

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
void client_destroy(client_t *client) 
{
  /* Remove the window */
  window_destroy(client->win);
  //free(client); Let the handler take care of this
  client->done = true;
  pthread_cond_signal(&client->thread_handler->thread_done_cond);
  pthread_exit(NULL);
}

void client_cleanup(client_t *client) 
{
  free(client);
}

static inline void wait_on_pause()
{
  // I'm not sure if this whole double if thing is necessary or not
  // I feel like it allows us to do a check check on pause (without
  // needing to lock it) but... yeah.
  if (g_thread_handler.pause) {
    pthread_mutex_lock(&g_thread_handler.pause_mutex);
    // Check the value again to make sure it didn't get swapped
    // out from under us.
    if (g_thread_handler.pause) {
      pthread_cond_wait(&g_thread_handler.pause_cond,
          &g_thread_handler.pause_mutex);
    }
    pthread_mutex_unlock(&g_thread_handler.pause_mutex);
  }
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
    wait_on_pause();
    handle_command(command, response, sizeof(response));
  }

  client_destroy(client);
  return 0;
}

int handle_command(char *command, char *response, int len) 
{
  if (command[0] == EOF) {
    strncpy(response, "all done", len - 1);
    return 0;
  }
  interpret_command(command, response, len);
  return 1;
}

void add_client_to_thread_handler(client_t *c)
{
  // Add this thread's info to the clients linked list
  // First lock the list
  pthread_mutex_lock(&g_thread_handler.clients_mutex);
  // Throw it on the beginning of the list
  c->next = g_thread_handler.clients;
  g_thread_handler.clients = c;
  // Unlock the clients list
  pthread_mutex_unlock(&g_thread_handler.clients_mutex);
}

void launch_client_thread(client_t *c)
{
  pthread_create(&c->thread, NULL, client_run, c); 
}

void create_client() 
{
  static int started = 0;
  client_t *c = NULL;

  c = client_create(started++);

  add_client_to_thread_handler(c);
  launch_client_thread(c);
}

void create_non_interactive_client(char* fin, char* fout)
{
  client_t *c = NULL;
  c = client_create_no_window(fin, fout);

  add_client_to_thread_handler(c);
  launch_client_thread(c);
}

void pause_clients()
{
  // No need to lock here for a write (I think...)
  g_thread_handler.pause = true;
}

void unpause_clients()
{
  if (!g_thread_handler.pause) { return; }

  // You DO need to lock here
  pthread_mutex_lock(&g_thread_handler.pause_mutex);
  g_thread_handler.pause = false;
  pthread_mutex_unlock(&g_thread_handler.pause_mutex);
  pthread_cond_broadcast(&g_thread_handler.pause_cond);
}

void wait_for_clients_to_exit()
{
  pthread_mutex_lock(&g_thread_handler.clients_mutex);
  pthread_cond_wait(&g_thread_handler.threads_done_cond, &g_thread_handler.clients_mutex);
  pthread_mutex_unlock(&g_thread_handler.clients_mutex);
}

void handle_main_command(char *command) 
{
  switch (command[0]) {
    case 'e':
      fprintf(stdout, "creating new interactive client\n");
      create_client();
      break;
    case 'E':
      {
        char **words = NULL;
        words = split_words(command);
        int num_words;
        for (num_words = 0; words[num_words] != NULL; ++num_words);
        if (num_words != 3) {
          fprintf(stdout, "ill-formed command");
          return;
        }
        fprintf(stdout, "creating new non interactive client from %s to %s\n", words[1], words[2]); 
        create_non_interactive_client(words[1], words[2]);
        free_words(words);
      }
      break;
    case 's':
      fprintf(stdout, "pausing clients\n");
      pause_clients();
      break;
    case 'g':
      fprintf(stdout, "unpausing clients\n");
      unpause_clients();
      break;
    case 'w':
      fprintf(stdout, "waiting for clients to exit\n");
      // Make sure the clients are unpaused
      unpause_clients();
      wait_for_clients_to_exit();
      fprintf(stdout, "now ready to take additional commands\n");
      break;
    default:
      fprintf(stdout, "ill-formed command\n");
      break;
  }
}

void reap_done_clients(client_t **client)
{
  if (*(client) == NULL) { return; } // There aren't any clients!

  while ((*client) != NULL) {
    if ((*client)->done) {
      int rc = 0;
      //This is one of the threads we're looking for!
      //So kill it!
      rc = pthread_join((*client)->thread, NULL);
      if (rc) {
        fprintf(stderr, "ERROR: pthread_join returned %d", rc);
      }
      fprintf(stdout, "Client %i has exited!\n", (*client)->id); 
      // Deallocate everything
      client_t *clean_me_up = *client;
      (*client) = (*client)->next;
      client_cleanup(clean_me_up);
    } 
    else {
      client = &(*client)->next;
    }
  }
}

void *thread_handler_main(void *arg)
{
  pthread_mutex_lock(&g_thread_handler.clients_mutex);
  for (;;) {
    pthread_cond_wait(&g_thread_handler.thread_done_cond, &g_thread_handler.clients_mutex);
    reap_done_clients(&g_thread_handler.clients);
    if (g_thread_handler.clients == NULL) {
      pthread_cond_broadcast(&g_thread_handler.threads_done_cond);
    }
  }
}

void init_thread_handler(thread_handler_t* t)
{
  t->clients = NULL;
  pthread_mutex_init(&t->clients_mutex, NULL);
  pthread_cond_init(&t->thread_done_cond, NULL);
  pthread_mutex_init(&t->pause_mutex, NULL);
  pthread_cond_init(&t->pause_cond, NULL);
  pthread_cond_init(&t->threads_done_cond, NULL);
  t->pause = false;
}

int main(int argc, char *argv[]) 
{
  char command[256] = { '\0' };
  //Initialize thread_handler
  init_thread_handler(&g_thread_handler);

  if (argc != 1) {
    fprintf(stderr, "Usage: server\n");
    exit(1);
  }

  // Launch the thread handler
  pthread_create(&g_thread_handler.thread, NULL, thread_handler_main, NULL);


  for (;;) {
    fprintf(stdout, ">>> ");
    fgets(command, sizeof(command), stdin);
    handle_main_command(command);
  }

  window_cleanup();
  return 0;
}
