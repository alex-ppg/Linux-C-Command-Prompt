#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "linux-prompt.h"

// Global Variable for changing directory
char * currentDirectory;

int main(int argc, char *argv[])
{
	printf(CLR);

	int status;
	char line[MAX_LINE_LENGTH];

	// Extract current directory from POSIX-compliant $PWD environment variable
	currentDirectory = getenv("PWD");
	if (strstr(currentDirectory, getenv("HOME")) != NULL)
	{
		// Duplicate currentDir for getting substring
		char *tmp = strdup(currentDirectory);
		// Add ~ character at start of output path
		strcpy(currentDirectory, "~");
		// Append the current directory at an offset of whatever the HOME directory is
		strcpy(tmp, currentDirectory + strlen(getenv("HOME")));
		// Join the strings
		strcat(currentDirectory, tmp);
		// Free the temporarily used memory
		free(tmp);
	}

	char hostname[20];

	// POSIX compliant gethostname syscall
	status = gethostname(hostname, 20);

	char* username = getenv("USER");

	printf(GRN BRT "%s@%s" RST ":" BLU BRT "%s" RST "> ", username, hostname, currentDirectory);

	int ch;
	while ((ch = getchar()) != EOF)
	{
		if (status == MAX_LINE_LENGTH)
		{
			printf(RED BRT "ERROR:" RST " Line length exceeded allowed size of %d, dropping rest of input\n", MAX_LINE_LENGTH - 1);

			// Drop rest of stdin as the enter character triggered the submission of all getchar()
			while ((ch = getchar()) != '\n');
		}
		else if (ch == '\n')
		{
			line[status] = '\0';

			// Returns non-zero value upon error
			if (sanitiseLine(line))
			{
				// Simply to prevent code repetition
				goto reset_console_visual;
			}

			// Check if the | character exists in line
			if (strchr(line, '|') != NULL)
			{
				// Save standard STDIN to resume after execution
				int mainSTDIN = dup(0);
				pipeExecute(line);
				dup2(mainSTDIN, 0);
			}
			else if (strstr(line, "cd") == line)
			{
				// Tokens will be "cd" "folder-name" "NULL" as Regex passed
				char * cArgs[3];
				parseArguments(cArgs, line);
				switchDirectory(cArgs[1]);
			}
			else if (strstr(line, "exit") != NULL)
			{
				return 0;
			}
			else
			{
				// Single command execution
				int childStatus;
				pid_t processID;

				if ((processID = fork()) == 0)
				{
					char *cArgs[MAX_PARAMS];
					parseArguments(cArgs, line);
					execute(cArgs);
				}
				else if (processID == -1)
				{
					perror(RED BRT "ERROR:" RST " Failed to create child process");
				}

				waitpid(processID, &childStatus, 0);
				if (childStatus) {
					printf(RED BRT "ERROR:" RST " Child w/ PID %d exited abnormally\n", processID);
				}
			}
		}
		else
		{
			// Add character to line and increment counter
			line[status] = ch;
			status++;
			continue;
		}

	reset_console_visual:
		// Reset line contents
		memset(line, 0, sizeof(line));
		// Restart counter at 0
		status = 0;
		// Re-print console prompt
		printf(GRN BRT "%s@%s" RST ":" BLU BRT "%s" RST "> ", username, hostname, currentDirectory);
	}
	printf("exit\n");
	return 0;
}

void execute(char ** cArgs)
{
	char * inputFilename = NULL;
	char * outputFilename = NULL;

	int i = 0, inStatus, outStatus;

	while (cArgs[++i] != NULL) {
		if (strcmp(cArgs[i], ">") == 0)
		{
			// Since execvp stops reading arguments on NULL, this will ensure normal execution of input function
			cArgs[i] = NULL;
			outputFilename = cArgs[++i];
		}
		else if (strcmp(cArgs[i], "<") == 0)
		{
			// See #137
			cArgs[i] = NULL;
			inputFilename = cArgs[++i];
		}
	}

	if (inputFilename != NULL)
	{
		// Read only access
		inStatus = open(inputFilename, O_RDONLY);
		if (inStatus == -1)
		{
			perror(RED BRT "ERROR:" RST " Failed to open output file in child process");
			exit(1);
		}
		// Attach input from file to process
		dup2(inStatus, 0);
		close(inStatus);
	}

	if (outputFilename != NULL)
	{
		// Write/Create access with 0644 file system permission flags
		outStatus = open(outputFilename, O_WRONLY | O_CREAT, 0644);
		if (outStatus == -1)
		{
			perror(RED BRT "ERROR:" RST " Failed to open output file in child process");
			exit(1);
		}
		// Attach output to file to process
		dup2(outStatus, 1);
		close(outStatus);
	}

	execvp(cArgs[0], cArgs);
	perror(RED BRT "ERROR:" RST " Failed to properly execute command in child process");
	exit(1);
}

void pipeExecute(char * fullLine)
{
	pid_t processID;
	int childStatus;

	do {

		char * pipeChar = strchr(fullLine, '|');

		if (pipeChar != NULL)
		{
			*pipeChar = '\0';
			pipeChar += 2;
		}

		// Detect cd before forking process to change working directory of parent process
		if (strstr(fullLine, "cd") == fullLine)
		{
			// Tokens will be "cd" "folder-name" "NULL" as Regex passed
			char * cArgs[3];
			parseArguments(cArgs, fullLine);
			switchDirectory(cArgs[1]);
		}
		else if (strstr(fullLine, "exit") != NULL)
		{
			kill(getpid(), SIGQUIT);
		}
		else
		{
			int pipeStatus[2];
			// Create in-out pipe
			pipe(pipeStatus);

			if ((processID = fork()) == 0)
			{
				if (pipeChar != NULL)
				{
					// If there is a next process, attach output to pipe
					dup2(pipeStatus[1], 1);
				}

				char * cArgs[MAX_PARAMS];
				parseArguments(cArgs, fullLine);
				execute(cArgs);
			}
			else if (processID == -1)
			{
				perror(RED BRT "ERROR:" RST " Failed to create child process");
			}

			// Attach input of pipe to parent
			dup2(pipeStatus[0], 0);
			// Close unused file descriptors
			close(pipeStatus[0]);
			close(pipeStatus[1]);

			// Wait for process to finish as it could err
			waitpid(processID, &childStatus, 0);

			if (childStatus)
			{
				printf(RED BRT "ERROR:" RST " Child w/ PID %d exited abnormally\n", processID);
				break;
			}
		}

		fullLine = pipeChar;
	} while (fullLine != NULL);
}

void parseArguments(char ** finalCommand, char * toParse)
{
	// Split line into tokens seperated by " ". The sanitiseLine function ensures that it is a parse-able command
	finalCommand[0] = strtok(toParse, " ");
	int i = 1;
	do
	{
		finalCommand[i] = strtok(NULL, " ");
	} while (finalCommand[i++] != NULL);
}

void switchDirectory(char * newDirectory)
{
	// Create a duplicate to iterate
	char * directoryParsed = strdup(currentDirectory);

	if (*(directoryParsed) == '~')
	{
		strcpy(directoryParsed, "");
		strcat(directoryParsed, getenv("HOME"));
		strcat(directoryParsed, currentDirectory + 1);
	}

	if (*(newDirectory) == '~')
	{
		char * goUp = strchr(strdup(newDirectory), '/');

		if (goUp != NULL)
		{
			// Checks that the extracted pointer of first '/' occurance has '~' before it and no '/' follow it
			if (*(goUp - 1) != '~' || *(goUp + 1) == '/')
			{
				printf(RED BRT "ERROR:" RST " Invalid path specified\n");
				return;
			}

			// Re-construct by pre-pending
			strcpy(newDirectory, "");
			strcat(newDirectory, getenv("HOME"));
			strcat(newDirectory, goUp);

			// Duplicate so that pointer can be cleaned up
			directoryParsed = strdup(newDirectory);

			// Skip the below loop as it is not necessary to parse path 
			*(newDirectory) = '\0';
		}
		else
		{
			// Checks that only the ~ character exists as the path
			if (*(newDirectory + 1) != '\0')
			{
				printf(RED BRT "ERROR:" RST " Invalid path specified\n");
				return;
			}
			strcpy(directoryParsed, getenv("HOME"));
			// Skip the below loop as it is not necessary to parse path
			*(newDirectory) = '\0';
		}
	}
	else if (*(newDirectory) == '/')
	{
		directoryParsed = strdup(newDirectory);
		*(newDirectory) = '\0';
	}

	// Begin loop to parse new path
	while (*(newDirectory) != '\0')
	{
		// . indicates relative positioning, same path
		if (*(newDirectory) == '.')
		{
			if (*(newDirectory + 1) == '/')
			{
				newDirectory += 2;
			}
			else if (*(newDirectory + 1) == '\0')
			{
				newDirectory++;
			}
			else if (*(newDirectory + 1) == '.')
			{
				// .. indicates relative positioning, one level above so we use strrchr which retrieves the last occurance compared to strchr
				char * goUp = strrchr(directoryParsed, '/');

				if (goUp != NULL)
				{
					// As we deal with null-terminated strings, assigning '\0' acts as shortening the string
					*goUp = '\0';
				}
				else
				{
					// Trying to go a level above the one allowed is prohibited
					printf(RED BRT "ERROR:" RST " Invalid path specified\n");
					return;
				}

				newDirectory += 2;
				if (*(newDirectory) == '/')
				{
					newDirectory++;
				}
				else if (*(newDirectory) != '\0')
				{
					// Catches paths in the form of ..a/..a
					printf(RED BRT "ERROR:" RST " Invalid path specified\n");
					return;
				}
			} else  {
				printf(RED BRT "ERROR:" RST " Invalid path specified\n");
				return;
			}
		}
		else 
		{
			if (strchr(newDirectory, '/') == NULL)
			{
				if (directoryParsed[strlen(directoryParsed) - 1] != '/')
				{
					strcat(directoryParsed, "/");
				}
				strcat(directoryParsed, newDirectory);
				break;
			}
			else
			{
				char * subFolder = strchr(newDirectory, '/');
				*subFolder = '\0';
				if (directoryParsed[strlen(directoryParsed) - 1] != '/')
				{
					strcat(directoryParsed, "/");
				}
				strcat(directoryParsed, newDirectory);
				newDirectory = subFolder + 1;
			}
		}
	}

	if (*(directoryParsed) == '\0')
	{
		strcpy(directoryParsed, "/");
	}
	else if (directoryParsed[strlen(directoryParsed) - 1] == '/')
	{
		// We want to remove the trailing slash ('/') from printing
		directoryParsed[strlen(directoryParsed) - 1] = '\0';
	}

	struct stat folderStat;

	// S_ISDIR is a POSIX syscall to check if a file is a directory while stat extracts the information about the file and stores it in our struct
	if (stat(directoryParsed, &folderStat) == 0)
	{
		if (S_ISDIR(folderStat.st_mode) || *(directoryParsed) == '/')
		{
			if (chdir(directoryParsed))
			{
				perror(RED BRT "ERROR:" RST " Failed to change directory");
				return;
			}
			currentDirectory = strdup(directoryParsed);
			if (strstr(currentDirectory, getenv("HOME")) != NULL)
			{
				// Duplicate current directory to 
				char *tmp = strdup(currentDirectory);
				// Add ~ character at start of output path
				strcpy(currentDirectory, "~");
				// Append the current directory at an offset of whatever the HOME directory is
				strcpy(tmp, currentDirectory + strlen(getenv("HOME")));
				// Join the strings
				strcat(currentDirectory, tmp);
				// Free the temporarily used memory
				free(tmp);
			}
			return;
		}
		printf(RED BRT "ERROR:" RST " Path specified does not contain a folder\n");
		return;
	}
	perror(RED BRT "ERROR:" RST " Failed to retrieve folder information");
	return;
}

int sanitiseLine(char * line)
{
	//
	// I constructed this Regex myself and it uses a few advanced features of Regex pattern matching
	//
	// Full Regex: ^(?(?=cd)(cd [^ ]+(?(?= )( \|)))|[^ ]*)(?(?= cd)( cd [^ ]+(?(?= )( \|)))| [^ ]*){0,}$
	//
	// Broken down:
	//
	// ^                Assert beginning of string
	// (?               Begin IF clause
	//   (?=cd)         If what lies ahead matches "cd" literally
	//   (cd [^ |]+      Match the string "cd " literally followed by at least one non-space non-| character
	//     (?           Begin IF clause
	//       (?= )      If what lies ahead matches " " literally
	//       ( \|)      Match the string " |" literally
	//     )            End IF clause
	//   )
	//   |              Else
	//   [^ ]+          Match one to unlimited non-space characters
	// )                End IF clause
	// (?               Begin IF clause
	//   (?= cd)        If what lies ahead matches " cd" literally
	//   ( cd [^ |]+     Match the string " cd " literally followed by at least one non-space non-| character
	//     (?           Begin IF clause
	//       (?= )      If what lies ahead matches " " literally
	//       ( \|)      Match the string " |" literally
	//     )            End IF clause
	//   )              
	//   |              Else
	//    [^ ]+         Match the string " " literally followed by one to unlimited non-space characters
	// )                End IF clause
	// {0,}             Match the previous IF clause zero to unlimited times
	// $                Assert end of string
	//
	// It is far from perfect but should achieve the purposes of this program functioning correctly within its scope
	//
	// EDIT: Sadly, the POSIX Regex (ERE) does not support look-aheads (The characters ?= as noted above actually check forward without consuming the string, like the match expressions do)
	//
	// Compiling the Regex using the POSIX standard regex with the REG_EXTENDED flag will not result in an error but rather cause a Segmentation Fault on any attempts to compare
	//
	// As such, the above explained Regex can only be used with the Perl Compatible Regular Expressions (PCRE) library in C which is non-POSIX standard
	//
	// Feel free to verify the Regex in action on the following link: https://regex101.com/r/VUZzJ4/2
	//
	// The approach contained here simply goes through the string character by character, implementing the above Regex in raw code with the inclusion of a check for >> and << as syntax errors
	//

	int match;

	char * iterationCopy = strdup(line);

	do
	{
		if (*(iterationCopy) == 'c' && *(++iterationCopy) == 'd')
		{
			if (*(++iterationCopy) != ' ')
			{
				goto no_match;
			}
			else
			{
				if (*(++iterationCopy) == ' ' || *(iterationCopy) == '\0')
				{
					goto no_match;
				}
				else
				{
					// Increment variable last to avoid getting pointer out of memory
					while (*(iterationCopy) != ' ' && *(iterationCopy) != '\0' && iterationCopy++);
					if (*(iterationCopy) != '\0' && (*(++iterationCopy) != '|' || *(++iterationCopy) != ' '))
					{
						goto no_match;
					}
				}
			}
		}
		else
		{
			if (*(iterationCopy) == ' ' && *(iterationCopy) != '\0')
			{
				goto no_match;
			}
			while (*(iterationCopy) != ' ' && *(iterationCopy) != '\0' && iterationCopy++);
			if (*(++iterationCopy) == ' ')
			{
				goto no_match;
			}
			else if (*(iterationCopy) == '|' && *(++iterationCopy) != ' ')
			{
				goto no_match;
			}
			else if (*(iterationCopy - 1) == '<' && *(iterationCopy) != ' ')
			{
				goto no_match;
			}
			else if (*(iterationCopy - 1) == '>' && *(iterationCopy) != ' ')
			{
				goto no_match;
			}
		}
	}
	while (*(++iterationCopy) != '\0');
	// Successful match
	return 0;

no_match:
	// No match
	printf(RED BRT "ERROR:" RST " Command improperly formatted\n", MAX_LINE_LENGTH - 1);
	return 1;
}
