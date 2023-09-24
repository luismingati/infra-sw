#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>
#include <pthread.h>
#include <fcntl.h>

int style = 0; // 0 = sequential, 1 = parallel

typedef struct Args
{
  char **args;
  int background; // 0 = foreground, 1 = background
} Args;

typedef struct Node
{
  Args command;
  struct Node *next;
} Node;

typedef struct
{
  char **firstArgs;
  char **secondArgs;
} PipeCommands;

typedef struct BgProcess
{
  pid_t pid;
  int jobNumber;
  struct BgProcess *next;
} BgProcess;

char *lastCommand = NULL;
BgProcess *bgProcesses = NULL;
int job_count = 0;

char *trim(char *str);
int execCommands(Args *arg);
int queueSize(Node *head);
void freeQueue(Node *head);
char *handleLastCommand(char *commandToken);
Node *createArgsQueue(char *buffer);
void enqueue(Node **head, Node **tail, Args args);
char **tokenizeBySpace(char *commandToken);
PipeCommands splitPipeArgs(char *commandToken);
int execPipe(PipeCommands *pipedCmds);
char *findRedirectionOperator(char *s);
void handleRedirection(Args *arg);
void addBgProcess(pid_t pid);
void removeBgProcess(pid_t pid);
void execFgCommand(char **tokens);
BgProcess *findBgProcess(int jobNumber);

int main(int argc, char *argv[])
{
  int pid = fork();
  if (pid == 0)
  {
    int should_run = 1;
    int batchMode = 0;
    FILE *file = NULL;

    if (argc == 2)
    {
      batchMode = 1;
    }

    if (argc > 2)
    {
      return 1;
    }

    if (batchMode)
    {
      file = fopen(argv[1], "r");
      if (!file)
      {
        printf("Error opening batch file");
        return 1;
      }
    }

    while (should_run)
    {
      char *buffer = NULL;
      size_t n = 0;
      ssize_t result;

      if (batchMode)
      {
        result = getline(&buffer, &n, file);
        if (result == -1)
        {
          break;
        }
      }
      else
      {
        if (style == 0)
        {
          printf("locm seq> ");
        }
        else if (style == 1)
        {
          printf("locm par> ");
        }
        fflush(stdout);
        result = getline(&buffer, &n, stdin);
        if (result == -1)
        {
          free(buffer);
          return 1;
        }
      }

      Node *head = createArgsQueue(buffer);
      Node *current = head;
      pthread_t threads[queueSize(head)];

      int i = 0;
      while (current)
      {
        Args *currentArg = (Args *)malloc(sizeof(Args));
        *currentArg = current->command;

        if (strcmp(currentArg->args[0], "fg") == 0)
        {
          execFgCommand(currentArg->args);
        }
        else if (strcmp(currentArg->args[0], "style") == 0)
        {
          if (currentArg->args[1] == NULL)
          {
            printf("style: option requires an argument \n");
            printf("Try 'style --help' for more information.\n");
          }
          else
          {
            if (strcmp(currentArg->args[1], "sequential") == 0)
            {
              style = 0;
            }
            else if (strcmp(currentArg->args[1], "parallel") == 0)
            {
              style = 1;
            }
            else if (strcmp(currentArg->args[1], "--help") == 0)
            {
              printf("Usage: style [OPTION]\n");
              printf("Set the execution style of the shell.\n\n");
              printf("Options:\n");
              printf("sequential\t executes commands one after the other.\n");
              printf("parallel  \t executes commands in parallel using multithreads.\n");
              printf("--help    \t display this help and exit.\n");
            }
            else if (strcmp(currentArg->args[1], "--help") != 0)
            {
              printf("style: invalid option '%s'\n", currentArg->args[1]);
              printf("Try 'style --help' for more information.\n");
            }
            else
            {
              printf("style: option requires an argument -- '%s'\n", currentArg->args[1]);
              printf("Try 'style --help' for more information.\n");
            }
          }
        }
        else
        {

          if (style == 0)
          {
            if (batchMode == 1)
            {
              printf("%s", currentArg->args[0]);
              for (int i = 1; currentArg->args[i] != NULL; i++)
              {
                printf(" %s", currentArg->args[i]);
              }
              printf("\n");
            }
            if (currentArg->args[0] && strcmp(currentArg->args[0], "exit") == 0)
            {
              should_run = 0;
              break;
            }
            execCommands(currentArg);
          }
          else if (style == 1)
          {
            if (batchMode == 1)
            {
              printf("%s", currentArg->args[0]);
              for (int i = 1; currentArg->args[i] != NULL; i++)
              {
                printf(" %s", currentArg->args[i]);
              }
              printf("\n");
            }
            if (currentArg->args[0] && strcmp(currentArg->args[0], "exit") == 0)
            {
              should_run = 0;
              if (current->next)
              {
                *currentArg = current->next->command;
              }
            }
            else
            {
              pthread_create(&threads[i], NULL, (void *)execCommands, currentArg);
              i++;
            }
          }
        }
        current = current->next;
      }

      for (int j = 0; j < i; j++)
      {
        pthread_join(threads[j], NULL);
      }

      if (should_run == 0)
      {
        break;
      }

      freeQueue(head);
      free(buffer);
    }
  }
  else
  {
    waitpid(pid, NULL, 0);
    return 0;
  }
}

Node *createArgsQueue(char *buffer)
{
  buffer[strcspn(buffer, "\n")] = 0;

  Node *head = NULL;
  Node *tail = NULL;
  char *outer_saveptr = NULL;
  buffer = trim(buffer);

  char *commandToken = strtok(buffer, ";");
  while (commandToken)
  {
    commandToken = trim(commandToken);

    int background = 0;
    int len = strlen(commandToken);
    if (len > 0 && commandToken[len - 1] == '&')
    {
      background = 1;
      commandToken[len - 1] = '\0';
      commandToken = trim(commandToken);
    }

    char *updatedCommand = handleLastCommand(commandToken);
    if (!updatedCommand)
    {
      commandToken = strtok(NULL, ";");
      continue;
    }

    char **argsList = NULL;
    if (strchr(updatedCommand, '|'))
    {
      argsList = malloc(2 * sizeof(char *));
      argsList[0] = updatedCommand;
      argsList[1] = NULL;
    }
    else
    {
      argsList = tokenizeBySpace(updatedCommand);
    }

    Args args;
    args.args = argsList;
    args.background = background;

    enqueue(&head, &tail, args);

    commandToken = strtok(NULL, ";");
  }
  return head;
}

void enqueue(Node **head, Node **tail, Args args)
{
  Node *newNode = malloc(sizeof(Node));
  newNode->command = args;
  newNode->next = NULL;

  if (!*head)
  {
    *head = newNode;
    *tail = newNode;
  }
  else
  {
    (*tail)->next = newNode;
    *tail = newNode;
  }
}

char **tokenizeBySpace(char *input)
{
  int spaces = 0;
  for (int i = 0; input[i]; i++)
  {
    if (input[i] == ' ' && input[i + 1] != ' ')
      spaces++;
  }

  char **tokens = malloc((spaces + 2) * sizeof(char *));
  if (!tokens)
  {
    perror("Failed to allocate memory for tokens");
    exit(EXIT_FAILURE);
  }

  int index = 0;
  char *currentPos = trim(input);
  char *nextSpace;

  while (*currentPos)
  {
    nextSpace = strchr(currentPos, ' ');
    if (!nextSpace)
    {
      tokens[index++] = strdup(currentPos);
      break;
    }
    tokens[index++] = strndup(currentPos, nextSpace - currentPos);
    currentPos = trim(nextSpace);
  }

  tokens[index] = NULL;
  return tokens;
}

char *handleLastCommand(char *commandToken)
{
  if (strcmp(commandToken, "!!") == 0)
  {
    if (lastCommand == NULL)
    {
      printf("No commands\n");
      return NULL;
    }
    else
    {
      return strdup(lastCommand);
    }
  }
  else
  {
    if (lastCommand != NULL)
      free(lastCommand);
    lastCommand = strdup(commandToken);
    return commandToken;
  }
}

char *trim(char *str)
{
  char *end;
  while (isspace((unsigned char)*str))
  {
    str++;
  }
  if (*str == 0)
  {
    return str;
  }
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
  {
    end--;
  }
  end[1] = '\0';
  return str;
}

int execCommands(Args *arg)
{
  if (strchr(arg->args[0], '|'))
  {
    PipeCommands pipedCmds = splitPipeArgs(arg->args[0]);
    if (pipedCmds.firstArgs && pipedCmds.secondArgs)
    {
      return execPipe(&pipedCmds);
    }
  }

  pid_t pid = fork();
  if (pid < 0)
  {
    fprintf(stderr, "Fork Failed");
    free(arg);
    return 1;
  }
  else if (pid == 0)
  {
    handleRedirection(arg);
    execvp(arg->args[0], arg->args);
    perror("locm seq> ");
    exit(EXIT_FAILURE);
  }
  else
  {
    if (arg->background)
    {
      addBgProcess(pid);
    }
    else
    {
      waitpid(pid, NULL, 0);
      free(arg);
    }
  }
  return 0;
}

int queueSize(Node *head)
{
  int size = 0;
  Node *current = head;
  while (current)
  {
    size++;
    current = current->next;
  }
  return size;
}

void freeQueue(Node *head)
{
  while (head)
  {
    Node *temp = head;
    head = head->next;
    free(temp->command.args);
    free(temp);
  }
}

PipeCommands splitPipeArgs(char *commandToken)
{
  PipeCommands pipedCmds;
  pipedCmds.firstArgs = NULL;
  pipedCmds.secondArgs = NULL;

  char *pipeLoc = strchr(commandToken, '|');
  if (pipeLoc)
  {
    *pipeLoc = '\0';
    pipeLoc++;
    pipeLoc = trim(pipeLoc);

    pipedCmds.firstArgs = tokenizeBySpace(commandToken);
    pipedCmds.secondArgs = tokenizeBySpace(pipeLoc);
  }

  return pipedCmds;
}

int execPipe(PipeCommands *pipedCmds)
{
    int fd[2];
    if (pipe(fd) < 0)
    {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }

    pid_t pid1 = fork();

    if (pid1 == 0)
    {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);

        if (execvp(pipedCmds->firstArgs[0], pipedCmds->firstArgs) < 0)
        {
            fprintf(stderr, "Command not found: %s\n", pipedCmds->firstArgs[0]);
            exit(EXIT_FAILURE);
        }
    }

    pid_t pid2 = fork();
    if (pid2 == 0)
    {
        close(fd[1]);
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);

        if (execvp(pipedCmds->secondArgs[0], pipedCmds->secondArgs) < 0)
        {
            fprintf(stderr, "Command not found: %s\n", pipedCmds->secondArgs[0]);
            exit(EXIT_FAILURE);
        }
    }

    close(fd[0]);
    close(fd[1]);

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    return 0;
}

char *findRedirectionOperator(char *string)
{
  if (strstr(string, ">>"))
  {
    return ">>";
  }
  if (strstr(string, ">"))
  {
    return ">";
  }
  if (strstr(string, "<"))
  {
    return "<";
  }
  return NULL;
}

void handleRedirection(Args *arg)
{
  for (int i = 0; arg->args[i]; i++)
  {
    char *operator= findRedirectionOperator(arg->args[i]);
    if (operator)
    {
      if (!arg->args[i + 1])
      {
        printf("Missign file\n");
        exit(EXIT_FAILURE);
      }

      int fd = -1;
      if (strcmp(operator, ">") == 0)
      {
        fd = open(arg->args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
      }
      else if (strcmp(operator, ">>") == 0)
      {
        fd = open(arg->args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
      }
      else if (strcmp(operator, "<") == 0)
      {
        fd = open(arg->args[i + 1], O_RDONLY);
      }

      if (fd == -1)
      {
        printf("Error opening file");
        exit(EXIT_FAILURE);
      }

      if (strcmp(operator, "<") == 0)
      {
        dup2(fd, STDIN_FILENO);
      }
      else
      {
        dup2(fd, STDOUT_FILENO);
      }

      close(fd);
      arg->args[i] = NULL;
      arg->args[i + 1] = NULL;
      break;
    }
  }
}

void addBgProcess(pid_t pid)
{
  BgProcess *newProcess = malloc(sizeof(BgProcess));
  newProcess->pid = pid;
  newProcess->jobNumber = ++job_count;
  newProcess->next = bgProcesses;
  bgProcesses = newProcess;

  printf("[%d] %d\n", newProcess->jobNumber, newProcess->pid);
}

void removeBgProcess(pid_t pid)
{
  BgProcess *current = bgProcesses;
  BgProcess *prev = NULL;
  while (current)
  {
    if (current->pid == pid)
    {
      if (prev)
      {
        prev->next = current->next;
      }
      else
      {
        bgProcesses = current->next;
      }
      free(current);
      return;
    }
    prev = current;
    current = current->next;
  }
}

BgProcess *findBgProcess(int jobNumber)
{
  BgProcess *current = bgProcesses;
  while (current)
  {
    if (current->jobNumber == jobNumber)
    {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

void execFgCommand(char **tokens)
{

  if (!tokens[1] || !isdigit(tokens[1][0]))
  {
    fprintf(stderr, "Usage: fg <jobNumber>\n");
    return;
  }

  int jobNumber = atoi(tokens[1]);
  BgProcess *process = findBgProcess(jobNumber);

  if (!process)
  {
    fprintf(stderr, "No such job: %d\n", jobNumber);
    return;
  }
  waitpid(process->pid, NULL, 0);
  removeBgProcess(process->pid);
}
