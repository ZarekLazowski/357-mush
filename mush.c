#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "parseline.h"

/*Variables that need to get reset after execution of a line.
 *Because they can get reset either after completion or after SIGINT, I 
 *decided to make them global variables to save some headache.
 */
static struct stage **stages = NULL;
static FILE *input;
static int numStage, status;
static char *line;
static pid_t *child;

void printIO(int i)
{
  fprintf(stderr, "This is process %d. Performing \"%s\"\n",
	  getpid(), stages[i]->argv[0]);
  fprintf(stderr, "   Input fd: %d\n", fileno(stdin));
  fprintf(stderr, "   output fd: %d\n", fileno(stdout));
}

/*Clean up the fields for the next run*/
void reset()
{
  int i;

  if( stages )
  {
    for(i = 0; i < numStage; i++)
    {
      clearStage(stages[i]);
    }

    free(stages);
    stages = NULL;
  }

  if( child )
  {
    free(child);
    child = NULL;
  }
  if( line )
  {
    free(line);
    line = NULL;
  }

  status = 0;
  numStage = 0;
}

/*Separates the command line into separate stages for parsing*/
int defineStages()
{
  char *stage, *place;
  
  /*Find first stage in command, break at pipes and newlines*/
  stage = strtok_r(line, "|\n", &place);

  /*Allocate memory for one stage*/
  stages = malloc(sizeof(struct stage *));

  /*While we are reading valid stages*/
  while( stage )
  {
    /*Allocate memory for the size of plus one stage*/
    stages = realloc(stages, sizeof(struct stage *) * (numStage + 1));

    /*Pass command to stage parser*/
    if( !parseStage(stage, numStage, &stages[numStage]) )
    {
      return 0;
    }
    
    /*stages[numStage] = parseStage(stage, numStage);*/
    numStage += 1;

    /*Find next stage, if applicable*/
    stage = strtok_r(NULL, "|\n", &place);
  }

  /*Function to visit each stage, if null, populate input/output with 
   *previous/next stage as applicable. If not null, report error and return. 
   */
  if( numStage > 1 )
  {
    if( !findPipes(stages, numStage) )
    {
      return 0;
    }
  }

  /*Everything was successful*/
  return 1;
}

/*Grabs the next line from the input, whether it be stdin or from a file*/
int grabLine()
{
  int len;

  /*Set up memory for the line*/
  line = malloc(sizeof(char) * MAXCMDLEN);

  /*Prompt user if in terminal*/
  if( isatty(fileno(input)) && isatty(fileno(stdout)) )
  {
    printf("8-P ");
    fflush(stdout);
  }
  
  /*Read input up to 1024 chars*/
  if( !(fgets(line, BUFLEN, input)) )
  {
    /*fgets returns null on EOF, when this happens print a new line if in the 
     *terminal, and return 0 to bigLoop*/
    if( isatty(fileno(input)) && isatty(fileno(stdout)) )
    {
      printf("\n");
      fflush(stdout);
    }

    return 0;
  }

  /*If the line begins with an EOF marker (for batch processing)*/
  if( *line == '' )
  {
    /*Return 0, no executable instructions*/
    return 0;
  }
  
  /*Check to see if command length is over the limit*/
  if( (len = strlen(line)) > MAXCMDLEN ) 
  {
    fprintf(stderr, "command too long\n");

    /*Return non-zero, but non-successful int on lines too long*/
    return 2;
  }
  
  /*Resize line to actual size (plus '\0')*/
  line = realloc(line, sizeof(char) * (len + 1));

  return 1;
}

/*Given a stage and the pair of input/output pipes, sets this process' 
 *input to the appropriate input. Whether it be stdin, an input file, or pipe. 
 */
void dupInput(struct stage *stage, int inPipe[2])
{
  int inFD;
  
  switch(stage->redirect[0])
  {
    /*Not redirected*/
    case(0):
      /*input is stdin, do nothing*/
      break;
    
    /*Redirected to a file*/
    case(1):
      /*Open described input file*/
      if( (inFD = open(stage->file[0], O_RDONLY)) == -1 )
      {
      	perror(stage->file[0]);
      	exit(EXIT_FAILURE);
      }

      /*Duplicate file FD over stdin*/
      if( (dup2(inFD, STDIN_FILENO)) == -1 )
      {
      	perror("file dup2 input");
      	exit(EXIT_FAILURE);
      }

      /*Close extra copy of input file*/
      close(inFD);
      break;
    
    /*Utilizing pipline structure*/
    case(2):
      /*Duplicate read end of inPipe over stdin*/
      if( (dup2(inPipe[0], STDIN_FILENO)) == -1 )
      {
      	perror("pipe dup2 input");
      	exit(EXIT_FAILURE);
      }
      break;
  }
}

/*Given a stage and the pair of input/output pipes, sets this process' 
 *output to the appropriate output. Whether it be stdout, an output file, 
 *or pipe. 
 */
void dupOutput(struct stage *stage, int outPipe[2])
{
  int outFD;
  
  switch(stage->redirect[1])
  {
    /*Not redirected*/
    case(0):
      /*output is stdout, do nothing*/
      break;
    
    /*Redirected to a file*/
    case(1):
      /*Open described input file*/
      if( (outFD = open(stage->file[1], O_WRONLY)) == -1 )
      {
      	perror(stage->file[1]);
      	exit(EXIT_FAILURE);
      }

      /*Duplicate file FD over stdin*/
      if( (dup2(outFD, STDOUT_FILENO)) == -1 )
      {
      	perror("file dup2 output");
      	exit(EXIT_FAILURE);
      }

      /*Close extra copy of input file*/
      close(outFD);
      break;
    
    /*Utilizing pipline structure*/
    case(2):
      /*Duplicate write end outPipe over stdin*/
      if( (dup2(outPipe[1], STDOUT_FILENO)) == -1 )
      {
      	perror("pipe dup2 output");
      	exit(EXIT_FAILURE);
      }
      break;
  }
}

/*If user prompts with "cd filename", 
 *change directory within the parent process*/
void changeDir()
{
  if( chdir((*stages)->argv[1]) == -1 )
  {
    perror((*stages)->argv[1]);
  }
}

/*For all other functions, fork off children and have it execute the command*/
void execChild()
{
  int i;
  int prev[2], next[2];  
  pid_t temp;

  sigset_t unblocked, blocked;

  /*Set up input pipe if there is more than one stage*/
  if(numStage > 1)
  {
    if( pipe(prev) )
    {
      perror("prev pipe");
      exit(EXIT_FAILURE);
    }
  }

  /*Create sigset of signals to block (SIGINT)*/
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGINT);

  /*Empty set of signals to set proc mask to*/
  sigemptyset(&unblocked);
  
  /*Block SIGINT*/
  if( sigprocmask(SIG_BLOCK, &blocked, NULL) == -1 )
  {
    perror("Blocking SIGINT");
    exit(EXIT_FAILURE);
  }

  /*Fork children, determine inputs/outputs, and execute given commands, 
   *based on number of stages and what the stages include in their 
   *structure*/
  for(i = 0; i < numStage; i++)
  {
    /*Set up out pipe if there is more than one stage*/
    if(numStage > 1 && i < (numStage - 1) )
    {
      if( pipe(next) )
      {
      	perror("next pipe");
      	exit(EXIT_FAILURE);
      }
    }

    /*Fork next child*/
    if( (temp = fork()) > 0 )
    {
      /*Parent branch*/

      /*Add the child id to the parent's list of children*/
      child[i] = temp;

      /*If more than one stage (pipes are being used)*/
      if( numStage > 1 )
      {
      	/*Close parent copies of previous pipes
      	 *... if we ever opened the pipe*/
      	close(prev[0]);
      	close(prev[1]);

      	/*Shift output pipes to the inputs for next stage*/
      	prev[0] = next[0];
      	prev[1] = next[1];
      }
    }
    else if(temp == 0)
    {
      /*Child branch*/

      /*Check to see where the input/output of the stage should go, 
       *respond accordingly*/
      dupInput(stages[i], prev);
      dupOutput(stages[i], next);

      /*Clean up child's copy of in/out pipes*/
      if( numStage > 1 )
      {
      	close(prev[0]);
      	close(prev[1]);
      	close(next[0]);
      	close(next[1]);
      }
      
      /*Unblock SIGINT*/
      if( sigprocmask(SIG_SETMASK, &unblocked, NULL) == -1 )
      {
      	perror("Unblocking SIGINT");
      	exit(EXIT_FAILURE);
      }

      /*Execute command from the stage*/
      if( execvp(stages[i]->argv[0], stages[i]->argv) == -1)
      {
      	perror(stages[i]->argv[0]);
      	exit(EXIT_FAILURE);
      }
    }
    else
    {
      /*Error branch*/
      perror("Fork");
      exit(EXIT_FAILURE);
    }
  }

  /*Unblock SIGINT*/
  if( sigprocmask(SIG_SETMASK, &unblocked, NULL) == -1 )
  {
    perror("Unblocking SIGINT");
    exit(EXIT_FAILURE);
  }

  /*Close next pipes*/
  if( numStage > 1 )
  {
    close(next[0]);
    close(next[1]);
  }

  /*Wait on children to make sure they complete*/
  for(i = 0; i < numStage; i++)
  {
    waitpid(child[i], &status, 0);
  }
}

/*Main loop where we grab the next line and (if there is a command), we attempt
 *to execute it. If the command is change directory, then the command is called
 *from within the parent process. Otherwise execChild is called, where the 
 *command is executed from the child process.
 */
void bigLoop()
{
  int lineReturn;
  
  /*continue until grabLine hits an EOF*/
  while( (lineReturn = grabLine()) )
  {
    /*If the line isn't just a new line*/
    if( lineReturn == 1 && strcmp(line, "\n") != 0)
    {
      /*Define stages, including args, in/out-puts, everything parseline does*/
      if( defineStages() )
      {
      	/*Malloc memory for the list of children*/
      	child = malloc(sizeof(pid_t) * numStage);

      	/*If the command given is the change directory command*/
      	if( strcmp((*stages)->argv[0], "cd") == 0 )
      	{
      	  /*Change directory within the parent process*/
      	  changeDir();
      	}
      	else
      	{
      	  /*If the command isn't cd, execute commands from child process*/
      	  execChild();
      	}
      }    
    }

    /*Reset the necessary variables for the next run*/
    reset();
  }
}

/*Signal handler:
 *Do not kill mush, wait for child to finish, reset, reprompt
 */
void handler(int signum)
{
  /*Print a new line to put next prompt on a new line*/
  printf("\n");
  
  /*If no children were created, reset and start the big loop over again*/
  if( !child )
  {
    reset();
    bigLoop();
  }

  /*If children were created, return back to wait for children*/
}

int main(int argc, char *argv[])
{
  struct sigaction sa;

  input = stdin;
  numStage = 0;
  
  /*Set up the signal handler with no mask*/
  sa.sa_handler = handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  /*Set up the signal handler*/
  if( sigaction(SIGINT, &sa, NULL) == -1 )
  {
    perror("Setting sigaction");
    exit(EXIT_FAILURE);
  }

  /*If a file was included in the arguments*/
  if( argc > 1 )
  {
    /*Attempt to open the given file in read only*/
    input = fopen(argv[1], "r");

    /*Report the error and exit if failed*/
    if( !input )
    {
      perror(argv[1]);
      exit(EXIT_FAILURE);
    }
  }
  
  bigLoop();
  
  fclose(input);
    
  return 0;
}
