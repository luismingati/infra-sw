#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>
#include <pthread.h>
#include <fcntl.h>

int style = 0; //0 = sequential, 1 = parallel

typedef struct Args {
    char **args;
    int background;  // 0 = foreground, 1 = background
} Args;

typedef struct Node {
    Args command;
    struct Node *next;
} Node;

typedef struct {
    char **firstArgs;
    char **secondArgs;
} PipeCommands;

typedef struct BgProcess {
    pid_t pid;
    int job_number;
    struct BgProcess* next;
} BgProcess;

char *lastCommand = NULL;
BgProcess* bg_processes = NULL;
int job_count = 0;

char *trim(char *str);
int execCommands(Args *arg);
int queueSize(Node *head);
void freeQueue(Node *head);
char* handleLastCommand(char *commandToken);
Node* createArgsQueue(char *buffer);
void enqueue(Node **head, Node **tail, Args args);
char** tokenizeBySpace(char *commandToken);
PipeCommands splitPipeArgs(char *commandToken);
int execPipe(PipeCommands *pipedCmds);
char *find_redir_operator(char *s);
void handleRedirection(Args *arg);
int execPipeParallel(PipeCommands *pipedCmds);
void addBgProcess(pid_t pid);
void removeBgProcess(pid_t pid);
void executeFgCommand(char** tokens);
BgProcess* findBgProcessByJobNumber(int job_number);

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

      if (strcmp(currentArg->args[0], "fg") == 0) {
        executeFgCommand(currentArg->args);
      } else if(strcmp(currentArg->args[0], "style") == 0) {
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
            printf("parallel  \t executes commands in parallel using multithreads.\n");
            printf("--help    \t display this help and exit.\n");
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

        // Verificando se o comando deve ser executado em background
        int background = 0;  // Por padrão, definido como foreground
        int len = strlen(commandToken);
        if (len > 0 && commandToken[len - 1] == '&') {
            background = 1;  // Marcar para executar em background
            commandToken[len - 1] = '\0';  // Remover o & do comando
            commandToken = trim(commandToken);  // Trim novamente após a remoção do &
        }

        char *updatedCommand = handleLastCommand(commandToken);
        if (!updatedCommand) {
            commandToken = strtok_r(NULL, ";", &outer_saveptr);
            continue;
        }

        char **argsList = NULL;
        if (strchr(updatedCommand, '|')) {
            argsList = malloc(2 * sizeof(char *));
            argsList[0] = updatedCommand;
            argsList[1] = NULL;
        } else {
            argsList = tokenizeBySpace(updatedCommand);
        }

        Args args;
        args.args = argsList;
        args.background = background;  // Adicione esta linha para setar a propriedade de background

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

char **tokenizeBySpace(char *input) {
  char **tokens = malloc(sizeof(input) * sizeof(char *));
  int index = 0;
  char *currentPos = input;
  char *nextSpace;
  char *redir;

  while (*currentPos) {
    nextSpace = strchr(currentPos, ' ');
    if (!nextSpace) {
      tokens[index++] = strdup(currentPos);
      break;
    }
    tokens[index++] = strndup(currentPos, nextSpace - currentPos);
    currentPos = nextSpace + 1;
  }
  tokens[index] = NULL;

  return tokens;
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
    if (strchr(arg->args[0], '|')) {
        PipeCommands pipedCmds = splitPipeArgs(arg->args[0]);
        if (pipedCmds.firstArgs && pipedCmds.secondArgs) {
            return execPipe(&pipedCmds);
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Fork Failed");
        free(arg);
        return 1;
    } else if (pid == 0) {  // Código do processo filho
        handleRedirection(arg);
        execvp(arg->args[0], arg->args);
        perror("locm seq> ");
        exit(EXIT_FAILURE);
    } else {  // Código do processo pai
        if (arg->background) {  // Se for para executar em background
            addBgProcess(pid);  // Adiciona o processo à lista de processos em background
        } else {
            waitpid(pid, NULL, 0);  // Espera o processo filho se for foreground
            free(arg);
        }
    }
    return 0;
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

PipeCommands splitPipeArgs(char *commandToken) {
  PipeCommands pipedCmds;
  pipedCmds.firstArgs = NULL;
  pipedCmds.secondArgs = NULL;

  char *pipeLoc = strchr(commandToken, '|');
  if (pipeLoc) {
    *pipeLoc = '\0';
    pipeLoc++;
    pipeLoc = trim(pipeLoc);

    pipedCmds.firstArgs = tokenizeBySpace(commandToken);
    pipedCmds.secondArgs = tokenizeBySpace(pipeLoc);
  }

  return pipedCmds;
}

int execPipe(PipeCommands *pipedCmds) {
    int fd[2];
    if (pipe(fd) < 0) {
        fprintf(stderr, "Pipe creation failed");
        return 1;
    }

    pid_t pid1 = fork();

    if (pid1 == 0) {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);

        if (execvp(pipedCmds->firstArgs[0], pipedCmds->firstArgs) < 0) {
            perror("locm seq> ");
            exit(EXIT_FAILURE);
        }
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(fd[1]);
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);

        if (execvp(pipedCmds->secondArgs[0], pipedCmds->secondArgs) < 0) {
            perror("locm seq> ");
            exit(EXIT_FAILURE);
        }
    }

    close(fd[0]);
    close(fd[1]);

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    return 0;
}

char *find_redir_operator(char *string) {
    if (strstr(string, ">>")) {
        return ">>";
    }
    if (strstr(string, ">")) {
        return ">";
    }
    if (strstr(string, "<")) {
        return "<";
    }
    return NULL;
}

void handleRedirection(Args *arg) {
  for (int i = 0; arg->args[i]; i++) {
      char *operator = find_redir_operator(arg->args[i]);
      if (operator) {
          if (!arg->args[i + 1]) {
              fprintf(stderr, "Error: missing filename after '%s'\n", operator);
              exit(EXIT_FAILURE);
          }

          int fd = -1;
          if (strcmp(operator, ">") == 0) {
              fd = open(arg->args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
          } else if (strcmp(operator, ">>") == 0) {
              fd = open(arg->args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
          } else if (strcmp(operator, "<") == 0) {
              fd = open(arg->args[i + 1], O_RDONLY);
          }

          if (fd == -1) {
              perror("Error opening file");
              exit(EXIT_FAILURE);
          }

          if (strcmp(operator, "<") == 0) {
              dup2(fd, STDIN_FILENO);
          } else {
              dup2(fd, STDOUT_FILENO);
          }

          close(fd);
          arg->args[i] = NULL;
          arg->args[i + 1] = NULL;
          break;
      }
  }
}

void addBgProcess(pid_t pid) {
    BgProcess* newProcess = malloc(sizeof(BgProcess));
    newProcess->pid = pid;
    newProcess->job_number = ++job_count;  // Incrementa o contador de jobs
    newProcess->next = bg_processes;
    bg_processes = newProcess;

    printf("[%d] %d\n", newProcess->job_number, newProcess->pid);
}

void removeBgProcess(pid_t pid) {
    BgProcess* current = bg_processes;
    BgProcess* prev = NULL;
    while (current) {
        if (current->pid == pid) {
            if (prev) {
                prev->next = current->next;
            } else {
                bg_processes = current->next;
            }
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

BgProcess* findBgProcessByJobNumber(int job_number) {
    BgProcess* current = bg_processes;
    while (current) {
        if (current->job_number == job_number) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void executeFgCommand(char** tokens) {
    // Se não há argumento após "fg" ou se o argumento não é um número
    if (!tokens[1] || !isdigit(tokens[1][0])) {
        fprintf(stderr, "Usage: fg <job_number>\n");
        return;
    }

    int job_number = atoi(tokens[1]);
    BgProcess* process = findBgProcessByJobNumber(job_number);

    if (!process) {
        fprintf(stderr, "No such job: %d\n", job_number);
        return;
    }

    // Espera o processo se completar
    waitpid(process->pid, NULL, 0);

    // Após esperar pelo processo, remova-o da lista de processos em background
    removeBgProcess(process->pid);
}
