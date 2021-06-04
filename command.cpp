#include "command.h"
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

SimpleCommand::SimpleCommand() {
  // just enough space for 5 args now
  _numberOfAvailableArguments = 5;
  _numberOfArguments = 0;
  _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument) {
  if (_numberOfAvailableArguments == _numberOfArguments + 1) {
    // double the available space
    _numberOfAvailableArguments *= 2;
    _arguments = (char **)realloc(_arguments,
                                  _numberOfAvailableArguments * sizeof(char *));
  }

  _arguments[_numberOfArguments] = argument;

  // null terminate the args
  _arguments[_numberOfArguments + 1] = NULL;

  _numberOfArguments++;
}

Command::Command() {
  // space for one simple command
  _numberOfAvailableSimpleCommands = 1;
  _simpleCommands = (SimpleCommand **)malloc(_numberOfSimpleCommands *
                                             sizeof(SimpleCommand *));

  _numberOfSimpleCommands = 0;
  _outFile = 0;
  _inputFile = 0;
  _errFile = 0;
  _background = 0;
  _openOptions = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand) {
  if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands) {
    _numberOfAvailableSimpleCommands *= 2;
    _simpleCommands = (SimpleCommand **)realloc(
        _simpleCommands,
        _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
  }

  _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
  _numberOfSimpleCommands++;
}

void Command::clear() {
  for (int i = 0; i < _numberOfSimpleCommands; i++) {
    for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++) {
      free(_simpleCommands[i]->_arguments[j]);
    }

    free(_simpleCommands[i]->_arguments);
    free(_simpleCommands[i]);
  }

  if (_outFile) {
    free(_outFile);
  }

  if (_inputFile) {
    free(_inputFile);
  }

  _numberOfSimpleCommands = 0;
  _outFile = 0;
  _inputFile = 0;
  _errFile = 0;
  _background = 0;
}

void Command::execute() {
  // nothing if no simple commands
  if (_numberOfSimpleCommands == 0) {
    prompt();
    return;
  }

  // save stdin / stdout / stderr
  int tempIn = dup(0);
  int tempOut = dup(1);
  int tempErr = dup(2);

  // set input
  int fdIn;
  if (_inputFile) { // open file for reading
    fdIn = open(_inputFile, O_RDONLY);
  } else { // use default input
    fdIn = dup(tempIn);
  }

  int i;
  int fdOut;
  pid_t child = 1;
  for (i = 0; i < _numberOfSimpleCommands; i++) {
    // redirect input and close fdIn since we're done with it
    dup2(fdIn, 0);
    close(fdIn);

    // setup output
    if (i == _numberOfSimpleCommands - 1) { // last simple command
      if (_outFile) {                       // redirect output
        mode_t openMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // -rw-r-----
        fdOut = open(_outFile, _openOptions, openMode);
      } else { // use default output
        fdOut = dup(tempOut);
      }

    } else { // not last simple command, so create a pipe
      int fdPipe[2];
      pipe(fdPipe);
      fdOut = fdPipe[1];
      fdIn = fdPipe[0];
    }

    dup2(fdOut, 1); // redirect output (stdout to fdOut)
    if (_errFile) { // redirect error at same location as output if necessary
      dup2(fdOut, 2);
    }
    close(fdOut); // close fdOut since we're done with it

    // check for special commands
    if (!strcmp(_simpleCommands[i]->_arguments[0], "exit")) { // exit
      exit(1);
    } else if (!strcmp(_simpleCommands[i]->_arguments[0], "cd")) {
      char *target;
      if (_simpleCommands[i]->_numberOfArguments == 1) {
        struct passwd *pw = getpwuid(geteuid());
        target = pw->pw_dir;
      } else if (_simpleCommands[i]->_numberOfArguments == 2) {
        target = _simpleCommands[i]->_arguments[1];
      } else {
        fprintf(stderr, "%s", "Invalid number of arguments\n");
        continue;
      }
      int ret = chdir(target);
      if (ret) {
        // chdir failed, perror it
        perror("cd");
      } else {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
          setenv("PWD", cwd, 1);
      }
      continue;
    } else if (!strcmp(_simpleCommands[i]->_arguments[0], "help")) {
      // ignore args
      fprintf(stdout, "%s",
              "Builtin commands:\n"
              "  cd: change directory\n"
              "  exit: to quit shell\n"
              "  help: to view this message\n");
      continue;
    } else {
      // else we fork!
      child = fork();
    }

    if (child == 0) {          // child process
      signal(SIGINT, SIG_DFL); // reset signal
      execvp(_simpleCommands[i]->_arguments[0], _simpleCommands[i]->_arguments);

      // if the child process reaches this point, then execvp must have failed
      perror("execvp");
      exit(1);
    } else if (child < 0) {
      fprintf(stderr, "Fork failed\n");
      exit(1);
    }

  } // endfor

  // restore in/out/err defaults
  dup2(tempIn, 0);
  dup2(tempOut, 1);
  dup2(tempErr, 2);
  close(tempIn);
  close(tempOut);
  close(tempErr);

  if (!_background) {
    waitpid(child, NULL, 0);
  }

  // clear to prepare for next command
  clear();

  // Print new prompt
  prompt();
}

void Command::prompt() {
  char hostname[HOST_NAME_MAX + 1];
  gethostname(hostname, HOST_NAME_MAX + 1);
  char cwd[PATH_MAX];
  char *cwd_ptr = cwd;
  getcwd(cwd, sizeof(cwd));
  uid_t euid = geteuid();
  struct passwd *pw = getpwuid(euid);
  size_t homedir_len = strlen(pw->pw_dir);
  if (pw->pw_dir != NULL && !strncmp(cwd, pw->pw_dir, homedir_len)) {
    cwd[homedir_len - 1] = '~';
    cwd_ptr += homedir_len - 1;
  }
  printf("[%s@%s %s]%c ", pw->pw_name, hostname, cwd_ptr,
         euid == 0 ? '#' : '$');
  fflush(stdout);
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int yyparse(void);

void sigint_handler(int /* p */) {
  printf("\n");
  Command::_currentCommand.clear();
  Command::_currentCommand.prompt();
}

int main() {
  struct sigaction sa;
  sa.sa_handler = sigint_handler;
  sa.sa_flags = SA_RESTART; // restart any interrupted system calls
  sigemptyset(&sa.sa_mask);

  // set the SIGINT handler
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  Command::_currentCommand.prompt();
  yyparse();
  return 0;
}
