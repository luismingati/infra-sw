#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define MAX_LINE 80

// - Execução interativa, saída com exit e style sequential - 10%
// •Exemplo: myLogin seq> /bin/ls; /bin/ps

int main(void) {
char *args[MAX_LINE/2 + 1]; /* command line arguments */
int should_run = 1; 

  while (should_run) {
    printf("locm >");
    fflush(stdout);
    char *command = NULL;
    char *temp[10];
    size_t n = 0;
    ssize_t result = getline(&command, &n, stdin);
    command[strcspn(command, "\n")] = 0;

    if(strcmp(command, "exit") == 0) {
      should_run = 0;
    }

    int i = 0;

    do {
      temp[0] = strtok(command, " ");
      while (temp[++i] != NULL) {
          temp[i] = strtok(NULL," ");
      }
    } while(strcmp(temp[0], "quit"));

    free(command);
    
    pid_t pid;
    pid = fork();

    if (pid < 0) {
      fprintf(stderr, "Fork Failed");
      return 1;
    } else if (pid == 0) {
      execvp(temp[0], temp);
    } else {
      wait(NULL);
    }
    
  }
  return 0;
}