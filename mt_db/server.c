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
#include <semaphore.h>

/* the encapsulation of a client thread, i.e., the thread that handles
 * commands from clients */
typedef struct Client client_t;
typedef struct ThreadHandler thread_handler_t;

struct Client {
  int id;
  pthread_t thread;
  window_t* win;
  bool done;
  thread_handler_t* thread_handler;
  struct Client* next;
};

struct ThreadHandler {
  client_t* clients;
  pthread_mutex_t clients_mutex;
  pthread_t thread;
  sem_t reaping_sem;
  bool pause;
  pthread_cond_t pause_cond;
  pthread_mutex_t pause_mutex;
  pthread_cond_t threads_done_cond;
};

int g_started = 0;

/* Interface with a client: get requests, carry them out and report results */
void *client_run(void *);
/* Interface to the db routines.  Pass a command, get a result */
int handle_command(char *, char *, int len);

//Global thread handler (just to make things simpler)
thread_handler_t g_thread_handler;

static inline void abort_if_null(void *value, char *message) {
  if (value == NULL) {
    fprintf(stdout, "%s\n", message);
    fprintf(stdout, "Aborting and dumping core\n");
    abort();
  }
}

/*
 * Create an interactive client - one with its own window.  This routine
 * creates the window (which starts the xterm and a process under it.  The
 * window is labelled with the ID passsed in.  On error, a NULL pointer is
 * returned and no process g_started.  The client data structure returned must be
 * destroyed using client_destroy()
 */
client_t *client_create(int ID) 
{
  client_t *new_Client = (client_t *) malloc(sizeof(client_t));
  if (!new_Client) return NULL;
  
  char title[16];
  new_Client->done = false;
  new_Client->next = NULL;
  new_Client->thread_handler = &g_thread_handler;
  new_Client->id = ID;

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
  // Grab a new id
  new_Client->id = g_started++;

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
  sem_post(&g_thread_handler.reaping_sem);
  pthread_exit(NULL);
}

void client_cleanup(client_t *client) 
{
  free(client);
}

/**
 * Clients call this in order to make sure that they wait if we are currently paused.
 *
 * I try to do some fancy stuff in order to avoid unnecessary locking, but it does not
 * seem to be noticeably faster than a simpler implementation that locks every time through.
 * This might need some more study, though.
 */
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

/**
 * This function simply locks the thread handler's
 * linked list of clients, adds the given client to it
 * and then unlocks the list
 */
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

/**
 * Simply call pthread_create on the given
 * clients thread member in order to get it
 * to run the client_run function
 */
void launch_client_thread(client_t *c)
{
  pthread_create(&c->thread, NULL, client_run, c); 
}

/**
 * Allocate and launch a client in a new thread.
 */
void create_client() 
{
  client_t *c = NULL;

  c = client_create(g_started++);

  // If the allocation failed we can't really do much...
  abort_if_null(c, "client_create() failed while creating client");

  // Make sure the thread handler knows about our new client
  add_client_to_thread_handler(c);
  // Actually start a new thread for the client and run it
  launch_client_thread(c);
}

/**
 * Allocate and launch a non interactive client in one go.
 */
void create_non_interactive_client(char* fin, char* fout)
{
  client_t *c = NULL;
  c = client_create_no_window(fin, fout);
  // Not much else we can do
  abort_if_null(c, "malloc() failed while creating non interactive client");

  add_client_to_thread_handler(c);
  launch_client_thread(c);
}

/**
 * Pause the clients.
 *
 * This function grabs the thread handler's pause_mutex,
 * sets its pause value to true, and then unlocks it.
 * 
 * This will cause the clients to stop executing commands the next
 * time that they run wait_on_pause
 */
void pause_clients()
{
  // No need to lock here for a write (I think...)
  // But I'm going to anyway (because I don't trust myself)
  pthread_mutex_lock(&g_thread_handler.pause_mutex);
  g_thread_handler.pause = true;
  pthread_mutex_unlock(&g_thread_handler.pause_mutex);
}

/**
 * Unpause the clients.
 *
 * Again, lock the mutex, set pause to false, and then
 * unlok the mutex.  After that just broadcast the condition
 * variable so that any threads that are currently waiting on
 * it will unpause.
 */
void unpause_clients()
{
  if (!g_thread_handler.pause) { return; }

  // You DO need to lock here
  pthread_mutex_lock(&g_thread_handler.pause_mutex);
  g_thread_handler.pause = false;
  pthread_mutex_unlock(&g_thread_handler.pause_mutex);
  pthread_cond_broadcast(&g_thread_handler.pause_cond);
}

/**
 * This function is called by the main function when it exits.
 *
 * This will cause any calling function to simply block while there
 * are still clients running by waiting on the threads_done_cond condition
 * variable until the thread handler broadcasts it.
 */
void wait_for_clients_to_exit()
{
  pthread_mutex_lock(&g_thread_handler.clients_mutex);
  if (g_thread_handler.clients != NULL) {
    pthread_cond_wait(&g_thread_handler.threads_done_cond, &g_thread_handler.clients_mutex);
  }
  pthread_mutex_unlock(&g_thread_handler.clients_mutex);
}

/**
 * Handle any one of the supported commands
 * say ill-formed command otherwise.
 */
void handle_main_command(char *command) 
{
  switch (command[0]) {
    case 'e':
      fprintf(stdout, "creating new interactive client\n");
      create_client();
      break;
    case 'E':
      {
        // Allocate a space on the stack to
        // throw a pointer to the words
        char **words = NULL;
        words = split_words(command);
        int num_words;
        // Count up the number of words returned
        for (num_words = 0; words[num_words] != NULL; ++num_words);
        // If it isn't 3 or 2 say it is an ill formed command and do nothing.
        // We allow 2 because the built in function allows the second argument
        // to be null if we just want to redirec client output to stdout
        if ((num_words != 3) && (num_words != 2)) {
          fprintf(stdout, "ill-formed command\n");
          return;
        }
        // If it worked announce what we're doing
        fprintf(stdout, "creating new non interactive client from %s to %s\n", words[1], words[2]); 
        create_non_interactive_client(words[1], words[2]);
        // Make sure we don't leak memory
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

/**
 * Called by the thread handler to join clients that have
 * completed.
 */
void reap_done_clients(client_t **client)
{
  // If there aren't any just return
  if (*client == NULL) { return; } // There aren't any clients!

  // Iterate over the clients
  for ( ;*client; client = &(*client)->next) {
    client_t *entry = *client;
    // If this client is done join with it, remove it from out list
    // and deallocate it.
    if (entry->done) {
      int rc = 0;
      //This is one of the threads we're looking for!
      //So kill it!
      rc = pthread_join(entry->thread, NULL);
      if (rc) {
        fprintf(stderr, "ERROR: pthread_join returned %d", rc);
      }
      fprintf(stdout, "Client %i has exited!\n", entry->id); 
      // Deallocate everything
      *client = entry->next;
      client_cleanup(entry);
      break;
    } 
  }
}

/**
 * The thread handler (which I also like to call the
 * thread reaper) simply sits around waiting to join with
 * threads that have finished.  It uses a semaphore in order
 * to do this so that it can handle multiple clients exiting at
 * the same time gracefully.
 */
void *thread_handler_main(void *arg)
{
  for (;;) {
    // Wait on a semaphore until a thread exits
    sem_wait(&g_thread_handler.reaping_sem);
    // Lock the clients list so we can use it
    pthread_mutex_lock(&g_thread_handler.clients_mutex);
    reap_done_clients(&g_thread_handler.clients);
    pthread_mutex_unlock(&g_thread_handler.clients_mutex);
    // If we just deleted the last client broadcast that we have done so
    if (g_thread_handler.clients == NULL) {
      pthread_cond_broadcast(&g_thread_handler.threads_done_cond);
    }
  }
}

/**
 * Initialize a new thred handler type
 */
void init_thread_handler(thread_handler_t* t)
{
  t->clients = NULL;
  pthread_mutex_init(&t->clients_mutex, NULL);
  pthread_mutex_init(&t->pause_mutex, NULL);
  pthread_cond_init(&t->pause_cond, NULL);
  pthread_cond_init(&t->threads_done_cond, NULL);
  t->pause = false;
  sem_init(&t->reaping_sem, 0, 0);
}

int main(int argc, char *argv[]) 
{
  char* command;
  size_t command_nbytes = 100;
  command = malloc(command_nbytes + 1);
  abort_if_null(command, "malloc() failed when allocating command");
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
    if (getline(&command, &command_nbytes, stdin) == -1) {
      if (feof(stdin)) {
        fprintf(stdout, "got EOF, waiting for clients to exit\n");
      } else {
        fprintf(stdout, "read error (not EOF), waiting for clients to exit\n");
      }
      wait_for_clients_to_exit();
      fprintf(stdout, "goodbye\n");
      exit(0);
    }
    handle_main_command(command);
  }

  window_cleanup();
  return 0;
}
