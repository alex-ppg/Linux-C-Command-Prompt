// Although it is obviously unnecessary for this project, we include a guard so that when multiple files that are linked under a single project import our header it doesn't get re-included
#ifndef ALX_CMD
# define ALX_CMD
// ANSI Escape Codes for colors (RED GREEN BLUE BRIGHT RESET)
# define RED "\x1B[31m"
# define GRN "\x1B[32m"
# define BLU "\x1B[34m"
# define PUR "\x1B[35m"
# define BRT "\x1B[1m"
# define RST "\x1B[0m"
// ANSI sequence for clearing screen in POSIX systems
# define CLR "\e[1;1H\e[2J"

// In order to accept 256 characters of input and store the terminator at the end (\0)
# define MAX_LINE_LENGTH 257
# define MAX_PARAMS 10

// The following function takes a NULL terminated Array of Strings and executes the command in the first position if any
void execute(char **);

// The following function takes a String as input and converts it to a NULL terminated Array of Strings by directly modifying the first argument
void parseArguments(char **, char *);

// The following function takes a String as input and pipes the execution of the commands seperated by the "|" character
void pipeExecute(char *);

// The following function changes the directory of the parent and updates the global variable to reflect that
void switchDirectory(char *);

// The following function imposes a low-level Regex on the input line to ensure that it is a valid command for our shell
int sanitiseLine(char *);

#endif