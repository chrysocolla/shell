%token <string_val> WORD

%token NOTOKEN NEWLINE GREAT LESS PIPE AMPERSAND GREATGREAT GREATGREATAMPERSAND GREATAMPERSAND

%union {
  char *string_val;
}

%{

#include <stdio.h>
#include <string.h>
#include <fcntl.h> // for open() arguments
#include "command.h"
void yyerror(const char * s);
int yylex();

%}

%%

goal            : commands
                | /* empty */
                ;

commands        : command
                | commands command 
                ;

command         : pipe_list iomodifier_list background_opt NEWLINE {
                  Command::_currentCommand.execute();
                }
                | NEWLINE { 
                  Command::_currentCommand.clear();
                  Command::_currentCommand.prompt();
                }
                | error NEWLINE { yyerrok; }
                ;

pipe_list       : pipe_list PIPE command_and_args
                | command_and_args 
                ;

command_and_args: command_word arg_list {
                  Command::_currentCommand.insertSimpleCommand(Command::_currentSimpleCommand);
                }
                ;

arg_list        : arg_list argument
                | /* empty */
                ;

argument        : WORD {
                  Command::_currentSimpleCommand->insertArgument($1);
                }
                ;

command_word    : WORD {
                  Command::_currentSimpleCommand = new SimpleCommand();
                  Command::_currentSimpleCommand->insertArgument($1);
                }
                ;

iomodifier_list : iomodifier_list iomodifier
                | /* empty */
                ;

iomodifier      : GREATGREAT WORD {
                  // append stdout to file
                  Command::_currentCommand._openOptions = O_WRONLY | O_CREAT;
                  Command::_currentCommand._outFile = $2;
                }
                | GREAT WORD {
                  // rewrite the file if it already exists
                  Command::_currentCommand._openOptions = O_WRONLY | O_CREAT | O_TRUNC;
                  Command::_currentCommand._outFile = $2;
                }
                | GREATGREATAMPERSAND WORD {
                  // redirect stdout and stderr to file and append
                  Command::_currentCommand._openOptions = O_WRONLY | O_CREAT;
                  Command::_currentCommand._outFile = $2;
                  Command::_currentCommand._errFile = $2;

                }
                | GREATAMPERSAND WORD {
                  // redirect stdout and stderr to file and truncate
                  Command::_currentCommand._openOptions = O_WRONLY | O_CREAT | O_TRUNC;
                  Command::_currentCommand._outFile = $2;
                  Command::_currentCommand._errFile = $2;

                }
                | LESS WORD {
                  Command::_currentCommand._inputFile = $2;
                } 
                ;

background_opt  : AMPERSAND {
                  Command::_currentCommand._background = 1;
                }
                | /* empty */
                ;

%%

void yyerror(const char * s) {
  fprintf(stderr, "%s", s);
}
