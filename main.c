#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

typedef struct Args {
  char **args;
  int size;
} Args;

typedef struct Node{
      Args args;
      struct Node *next;
} Node;

Args createArgs(char *buffer);
void enqueue(Node **head, Node **tail, Args args);
void dequeue(Node **head, Node **tail);
void printFila(Node *head);

int main(void) {
  int should_run = 1;
  Node *head = NULL;
  Node *tail = NULL;

  while (should_run) {
    printf("locm >");
    fflush(stdout);

    char *buffer = NULL;
    size_t n = 0;
    ssize_t result = getline(&buffer, &n, stdin);
    
    if (result == -1) {
      free(buffer);
      continue;
    }
    //if in buffer has ; then split and execute
    
    Args args = createArgs(buffer);
    

    enqueue(&head, &tail, args);
    printFila(head);
    if (args.args[0] && strcmp(args.args[0], "exit") == 0) {
      free(buffer);
      free(args.args);
      break;
    }

    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "Fork Failed");
      free(buffer);
      free(args.args);
      return 1;
    } else if (pid == 0) {
      execvp(args.args[0], args.args);
      perror("locm >");  
      exit(EXIT_FAILURE);
    } else {
      wait(NULL);
    }
    
    free(buffer);
    free(args.args);
  }

  return 0;
}

Args createArgs(char *buffer) {
    int bufferLength = strlen(buffer);
    char **argsList = malloc((bufferLength / 2 + 1) * sizeof(char *));

    buffer[strcspn(buffer, "\n")] = 0;

    char *token = strtok(buffer, " ");
    int index = 0;
    while (token != NULL) {
      argsList[index] = token;
      token = strtok(NULL, " ");
      index++;
    }
    argsList[index] = NULL;

    Args args;
    args.args = argsList;
    args.size = index;
    return args;
}

void enqueue(Node **head, Node **tail, Args args) {
  Node *new = (Node *)malloc(sizeof(Node));
  if (new != NULL) {
    new->args = args;
    new->next = NULL;

    if (*head == NULL) {
      *head = new;
      *tail = new;
    } else {
      (*tail)->next = new;
      *tail = new;
    }
  } 
}

void dequeue(Node **head, Node **tail) {

  Node *aux;
  if ((*head) != NULL) {
    aux = *head;
    *head = (*head)->next;
    free(aux);
    if ((*head) == NULL) 
      *tail = NULL;
  }
}

void printFila(Node *head) {
  int i = 0;
  while (head != NULL) {
    printf("%s-> ", head->args.args[i]);
    i++;
    head = head->next;
  }
  printf("NULL \n");
}