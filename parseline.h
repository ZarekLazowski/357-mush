#ifndef PARSELINEH

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAXCMDLEN 512
#define BUFLEN 1024

/*Structure for a stage, to be passed to a function to get whatever it needs
 *out of the structure. Primarily a product of parsing that can be shown off
 *in the parseline assignment or executed in the MUSH assignment.
 
 *For fields that are arrays, the first value represents the input/output,
 *with 0 representing the input and 1 representing the output
 */
struct stage
{
  int stageNum;         /*Step number for execution*/
  int redirect[2];      /*Redirected flag: 0-not 1-redirected 2-pipe*/
  char *file[2];        /*Input/output filename, Null is considered stdin*/
  struct stage *pipe[2];   /*Stage to receive/push data from/to*/
  int argc;             /*Count of arguments to be passed during execution*/
  char **argv;          /*Malloced vector listing all arguments to passed*/
  char *cmd;            /*Full unparsed command string*/
  char *mangled;        /*Pointer to malloc-ed string fed to strtok*/
};

void clearStage(struct stage *stage);

void printStage(struct stage *stage);

int setInput(struct stage **list, int current);

int setOutput(struct stage **list, int current);

int findPipes(struct stage **list, int numStages);

int parseStage(char *command, int num, struct stage **stage);

#endif
