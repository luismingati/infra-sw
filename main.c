#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

int main(void) {
  int should_run = 1;

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

    int bufferLength = strlen(buffer);
    char *args[bufferLength / 2 + 1];
    
    buffer[strcspn(buffer, "\n")] = 0;

    char *token = strtok(buffer, " ");
    int index = 0;
    while (token != NULL) {
      args[index] = token;
      token = strtok(NULL, " ");
      index++;
    }
    args[index] = NULL;

    if (args[0] && strcmp(args[0], "exit") == 0) {
      free(buffer);
      break;
    }

    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "Fork Failed");
      free(buffer);
      return 1;
    } else if (pid == 0) {
      execvp(args[0], args);
      perror("locm >");  
      exit(EXIT_FAILURE);
    } else {
      wait(NULL);
    }
    
    free(buffer);
  }

  return 0;
}
