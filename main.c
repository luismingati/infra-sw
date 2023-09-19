#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>
#include <pthread.h>

int style = 0; //0 = sequential, 1 = parallel

typedef struct Args {
  char **args;
} Args;

typedef struct Node {
    Args command;
    struct Node *next;
} Node;

char *lastCommand = NULL;

Node* createArgsQueue(char *buffer);
char *trim(char *str);
int execCommands(Args *arg);
int queueSize(Node *head);
void freeQueue(Node *head);
char* handleLastCommand(char *commandToken);
Node* createArgsQueue(char *buffer);
void enqueue(Node **head, Node **tail, Args args);
char** tokenizeBySpace(char *commandToken);
void execCommandsInBackground();


int main(int argc, char *argv[]) {
  int should_run = 1;
  int batchMode = 0;
  FILE *file = NULL;

  if(argc == 2) {
    batchMode = 1;
  }

  if (batchMode) {
    file = fopen(argv[1], "r");
    if (!file) {
      printf("Error opening batch file");
      return 1;
    }
  }

  while (should_run) {
    char *buffer = NULL;
    size_t n = 0;
    ssize_t result;

    if (batchMode) {
      result = getline(&buffer, &n, file);
      if (result == -1) {
        break;
      }
    } else {
      if(style == 0) {
        printf("locm seq> ");
      } else if(style == 1) {
        printf("locm par> ");
      }
      fflush(stdout);
      result = getline(&buffer, &n, stdin);
      if (result == -1) {
        free(buffer);
        continue;
      }
    }

    Node *head = createArgsQueue(buffer);
    Node *current = head;
    pthread_t threads[queueSize(head)];

    int i = 0;
    while (current) {
      Args *currentArg = (Args *)malloc(sizeof(Args));
      *currentArg = current->command;

      if(strcmp(currentArg->args[0], "style") == 0) {
        if(currentArg->args[1] == NULL) {
          printf("style: option requires an argument \n");
          printf("Try 'style --help' for more information.\n");
        } else {
          if(strcmp(currentArg->args[1], "sequential") == 0) {
            style = 0;
        } else if(strcmp(currentArg->args[1], "parallel") == 0) {
            style = 1;
        } else if (strcmp(currentArg->args[1], "--help") == 0) {
            printf("Usage: style [OPTION]\n");
            printf("Set the execution style of the shell.\n\n");
            printf("Options:\n");
            printf("sequential\t executes commands one after the other.\n");
            printf("parallel\t executes commands in parallel using multithreads.\n");
            printf("--help\t display this help and exit.\n");
        } else if(strcmp(currentArg->args[1], "--help") != 0) {
            printf("style: invalid option '%s'\n", currentArg->args[1]);
            printf("Try 'style --help' for more information.\n");
        } else {
            printf("style: option requires an argument -- '%s'\n", currentArg->args[1]);
            printf("Try 'style --help' for more information.\n");
          }
        }
      } else {

        if (style == 0) {
          if (currentArg->args[0] && strcmp(currentArg->args[0], "exit") == 0) {
            should_run = 0;
            break;
          }
          execCommands(currentArg);

        } else if (style == 1) {

          if (currentArg->args[0] && strcmp(currentArg->args[0], "exit") == 0) {
            should_run = 0;
            if(current->next) {
              *currentArg = current->next->command;
            }
          } else {
            pthread_create(&threads[i], NULL, (void*)execCommands, currentArg);
            i++;
          }

        }
      }
      current = current->next;
    }

    for (int j = 0; j < i; j++) {
      pthread_join(threads[j], NULL);
    }

    if(should_run == 0) {
      break;
    }

    freeQueue(head);
    free(buffer);
  }

  return 0;
}

Node* createArgsQueue(char *buffer) {
  buffer[strcspn(buffer, "\n")] = 0;

  Node *head = NULL;
  Node *tail = NULL;
  char *outer_saveptr = NULL;

  char *commandToken = strtok_r(buffer, ";", &outer_saveptr);
  while (commandToken) {
    commandToken = trim(commandToken);

    char *updatedCommand = handleLastCommand(commandToken);
    if (!updatedCommand) {
      commandToken = strtok_r(NULL, ";", &outer_saveptr);
      continue;
    }

    char **argsList = tokenizeBySpace(updatedCommand);

    Args args;
    args.args = argsList;

    enqueue(&head, &tail, args);

    commandToken = strtok_r(NULL, ";", &outer_saveptr);
  }
  return head;
}

void enqueue(Node **head, Node **tail, Args args) {
  Node *newNode = malloc(sizeof(Node));
  newNode->command = args;
  newNode->next = NULL;

  if (!*head) {
    *head = newNode;
    *tail = newNode;
  } else {
    (*tail)->next = newNode;
    *tail = newNode;
  }
}

char** tokenizeBySpace(char *commandToken) {
  char **argsList = malloc((strlen(commandToken) + 1) * sizeof(char *));
  int index = 0;
  char *inner_saveptr = NULL;
  char *argToken = strtok_r(commandToken, " ", &inner_saveptr);

  while (argToken) {
    argsList[index] = argToken;
    argToken = strtok_r(NULL, " ", &inner_saveptr);
    index++;
  }
  argsList[index] = NULL;

  return argsList;
}

char* handleLastCommand(char *commandToken) {
  if (strcmp(commandToken, "!!") == 0) {
    if (lastCommand == NULL) {
      printf("No commands\n");
      return NULL;
    } else {
      return strdup(lastCommand);
    }
  } else {
    if (lastCommand != NULL) free(lastCommand);
    lastCommand = strdup(commandToken);
    return commandToken;
  }
}

char *trim(char *str) {
  char *end;
  while(isspace((unsigned char)*str)) {
    str++;
  }
  if(*str == 0) {
    return str;
  }
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) {
    end--;
  }
  end[1] = '\0';
  return str;
}

int execCommands(Args *arg) {
  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "Fork Failed");
    free(arg);
    return 1;
  } else if (pid == 0) {
    execvp(arg->args[0], arg->args);
    perror("locm seq> ");
    exit(EXIT_FAILURE);
  } else {
    wait(NULL);
    free(arg);
  }
}

int queueSize(Node *head) {
  int size = 0;
  Node *current = head;
  while (current) {
    size++;
    current = current->next;
  }
  return size;
}

void freeQueue(Node *head) {
  while (head) {
    Node *temp = head;
    head = head->next;
    free(temp->command.args);
    free(temp);
  }
}

// Node* createArgsQueue(char *buffer) {
//   buffer[strcspn(buffer, "\n")] = 0;

//   Node *head = NULL;
//   Node *tail = NULL;
//   char *outer_saveptr = NULL, *inner_saveptr = NULL;

//   char *commandToken = strtok_r(buffer, ";", &outer_saveptr);
//   while (commandToken) {
//     commandToken = trim(commandToken);

//     if (strcmp(commandToken, "!!") == 0) {
//       if (lastCommand == NULL) {
//         printf("No commands\n");
//         commandToken = strtok_r(NULL, ";", &outer_saveptr);
//         continue;
//       } else {
//         commandToken = strdup(lastCommand);
//       }
//     } else {
//       if (lastCommand != NULL) free(lastCommand);
//       lastCommand = strdup(commandToken);
//     }

//     char **argsList = malloc((strlen(commandToken) + 1) * sizeof(char *));
//     int index = 0;
//     char *argToken = strtok_r(commandToken, " ", &inner_saveptr);

//     while (argToken) {
//       argsList[index] = argToken;
//       argToken = strtok_r(NULL, " ", &inner_saveptr);
//       index++;
//     }
//     argsList[index] = NULL;

//     Args args;
//     args.args = argsList;

//     Node *newNode = malloc(sizeof(Node));
//     newNode->command = args;
//     newNode->next = NULL;

//     if (!head) {
//       head = newNode;
//       tail = newNode;
//     } else {
//       tail->next = newNode;
//       tail = newNode;
//     }
//     commandToken = strtok_r(NULL, ";", &outer_saveptr);
//   }
//   return head;
// }
