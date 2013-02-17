#include <pthread.h>

typedef struct Node {
	char *name;
	char *value;
	struct Node *lchild;
	struct Node *rchild;
        pthread_rwlock_t rwlock;
} node_t;

extern node_t head;

void interpret_command(char *, char *, int);
