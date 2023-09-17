#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>
#include <pthread.h>

typedef struct Args {
  char **args;
  int size;
} Args;

typedef struct Node {
    Args command;
    struct Node *next;
} Node;

Node* createArgsQueue(char *buffer);
char *trim(char *str);
int execCommands(Args *arg);
int queueSize(Node *head);
void freeQueue(Node *head);

int main(void) {
  int should_run = 1;

  while (should_run) {
    printf("locm seq> ");
    fflush(stdout);

    char *buffer = NULL;
    size_t n = 0;
    ssize_t result = getline(&buffer, &n, stdin);

    if (result == -1) {
        free(buffer);
        continue;
    }

    Node *head = createArgsQueue(buffer);
    Node *current = head;

    pthread_t threads[queueSize(head)];

    while (current) {
      Args *currentArg = (Args *)malloc(sizeof(Args));
      *currentArg = current->command;

      if (currentArg->args[0] && strcmp(currentArg->args[0], "exit") == 0) {
        should_run = 0;
        break;
      }

      execCommands(currentArg);

      current = current->next;
    }

    // int i = 0;
    // while (current) {
    //   Args *currentArg = (Args *)malloc(sizeof(Args));
    //   *currentArg = current->command;

    //   if (currentArg->args[0] && strcmp(currentArg->args[0], "exit") == 0) {
    //     should_run = 0;
    //     if(current->next) {
    //       *currentArg = current->next->command;
    //     }
    //   } else {
    //     pthread_create(&threads[i], NULL, (void*)execCommands, currentArg);
    //     i++;
    //   }

    //   current = current->next;
    // }

    // for (int j = 0; j < i; j++) {
    //   pthread_join(threads[j], NULL);
    // }

    if(should_run == 0) {
      break;
    }

    freeQueue(head);
    free(buffer);
  }

  return 0;
}

//perguntar: Quando o usuario usa o comando git commit -m "a b c" ele entende ""a" "b" "c"" como strings separadas, é bug
Node* createArgsQueue(char *buffer) {
  buffer[strcspn(buffer, "\n")] = 0;

  Node *head = NULL;
  Node *tail = NULL;
  char *outer_saveptr = NULL, *inner_saveptr = NULL;

  char *commandToken = strtok_r(buffer, ";", &outer_saveptr);
  while (commandToken) {
    commandToken = trim(commandToken);
    char **argsList = malloc((strlen(commandToken) + 1) * sizeof(char *));
    int index = 0;
    char *argToken = strtok_r(commandToken, " ", &inner_saveptr);

    while (argToken) {
      argsList[index] = argToken;
      argToken = strtok_r(NULL, " ", &inner_saveptr);
      index++;
    }
    argsList[index] = NULL;

    Args args;
    args.args = argsList;
    args.size = index;

    Node *newNode = malloc(sizeof(Node));
    newNode->command = args;
    newNode->next = NULL;

    if (!head) {
      head = newNode;
      tail = newNode;
    } else {
      tail->next = newNode;
      tail = newNode;
    }
    commandToken = strtok_r(NULL, ";", &outer_saveptr);
  }
  return head;
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
