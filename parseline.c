#include <ctype.h>
#include "parseline.h"

/*Clears all memory associated with the stages*/
void clearStage(struct stage *stage)
{
  /*Free mangled string from the parsestage function*/
  free(stage->mangled);

  /*Free the list of arguments*/
  free(stage->argv);

  /*Free the stage structure itself*/
  free(stage);
}

/*Print out the requested information in each stage according to the format*/
void printStage(struct stage *stage)
{
  int i;

  /*Pad beginning with new line*/
  printf("\n--------\n");
  printf("Stage %d: \"%s\"\n", stage->stageNum, stage->cmd);
  printf("--------\n");

  /*Determine proper input for current stage*/
  switch (stage->redirect[0])
  {
    case (0):
      printf("     input: original stdin\n");
      break;
    case (1):
      printf("     input: %s\n", (stage->file[0]) );
      break;
    case (2):
      printf("     input: pipe from stage %d\n",
	     ((stage->pipe[0])->stageNum) );
      break;
    default:
      printf("Double check redirections for input\n");
      exit(EXIT_FAILURE);
  }

  /*Determine proper output for current stage*/
  switch (stage->redirect[1])
  {
    case (0):
      printf("    output: original stdout\n");
      break;
    case (1):
      printf("    output: %s\n", (stage->file[1]) );
      break;
    case (2):
      printf("    output: pipe to stage %d\n",
	     ((stage->pipe[1])->stageNum) );
      break;
    default:
      printf("Double check redirections for output\n");
      exit(EXIT_FAILURE);
  }
  
  printf("      argc: %d\n", stage->argc);
  printf("      argv: ");

  /*Scroll through arguments in argv*/
  for(i = 0; i < stage->argc; i++)
  {
    if(i == ((stage->argc) - 1))
    {
      printf("\"%s\"\n", stage->argv[i]);
    }
    else
    {
      printf("\"%s\",", stage->argv[i]);
    }
  }
}

/*Set input of this stage to the previous stage*/
int setInput(struct stage **list, int current)
{
  /*If the input has not been redirected*/
  if( (list[current]->redirect[0]) == 0 )
  {
    /*Mark redirection as to a pipe*/
    list[current]->redirect[0] = 2;

    /*Set the previous stage as the input pipe*/
    list[current]->pipe[0] = list[current-1];
  }
  /*If the input has been set*/
  else
  {
    fprintf(stderr, "%s: ambiguous input\n", list[current]->argv[0]);
    return 0;
  }

  return 1;
}

/*Set the output of this stage to next stage*/
int setOutput(struct stage **list, int current)
{
  /*If the output has not been redirected*/
  if( (list[current]->redirect[1]) == 0 )
  {
    /*Mark redirection as to a pipe*/
    list[current]->redirect[1] = 2;
    
    /*Set the next stage as the output pipe*/
    list[current]->pipe[1] = list[current+1];
  }
  /*If the output has been set*/
  else
  {
    fprintf(stderr, "%s: ambiguous output\n", list[current]->argv[0]);
    return 0;
  }

  return 1;
}

/*If more than one stage exists, then pipes are involved. This function will
 *set the inputs and outputs of each stage to the previous and next stage 
 *respectively. However if the current stage is the first or last, only the 
 *output or input is set, respectively. If this program encounters a non-null
 *input/output, it will error out and print the malformed command.
 */
int findPipes(struct stage **list, int numStages)
{
  int i;

  for(i = 0; i < numStages; i++)
  {
    /*First stage*/
    if( i == 0 )
    {
      if( !setOutput(list, i) )
      {
	      return 0;
      }
    }
    /*Last stage*/
    else if( i == (numStages - 1) )
    {
      if( !setInput(list, i) )
      {
	      return 0;
      }
    }
    /*Middle stage(s)*/
    else
    {
      if( !setInput(list, i) || !setOutput(list, i) )
      {
	      return 0;
      }
    }
  }

  /*Everything was successful*/
  return 1;
}

/*Pass a malloc-ed string containing the command to be executed and which 
 *stage it is, returns a stage structure that fills out the cmd, argc, argv, 
 *and stageNum. If a < or > exists, will populate the input/output field with
 *the following argument. Otherwise, a null input/output field is considered
 *stdin/stdout.
 */
int parseStage(char *command, int num, struct stage **stage)
{
  char *arg, *trimCmd, *cmdcpy;
  
  /*Check to see if there is more than one < or > argument, print out 
   *malformed command and exit if so*/
  if( strchr(command, '<') != strrchr(command, '<') )
  {
    fprintf(stderr, "%s: bad input redirection\n", command);
    return 0;
  }
  if( strchr(command, '>') != strrchr(command, '>') )
  {
    fprintf(stderr, "%s: bad output redirection\n", command);
    return 0;
  }

  /*Set pointer for finding the first non-blank char*/
  trimCmd = command;

  /*Skip over blank spaces, if applicable*/
  while( isblank(*trimCmd) )
  {
    trimCmd++;
  }

  if( strcmp(trimCmd, "") == 0 )
  {
    fprintf(stderr, "invalid null command\n");
    return 0;
  }
  
  /*Alloc mem for the stage if no input/output errors were found*/
  (*stage) = malloc(sizeof(struct stage));

  /*Initialize stage fields*/
  (*stage)->stageNum = num;
  (*stage)->redirect[0] = 0;
  (*stage)->redirect[1] = 0;
  (*stage)->file[0] = NULL;
  (*stage)->file[1] = NULL;
  (*stage)->pipe[0] = NULL;
  (*stage)->pipe[1] = NULL;
  (*stage)->cmd = trimCmd;
  (*stage)->argc = 0;

  (*stage)->argv = malloc(sizeof(char*) * 2);
  
  /*Copy mutable version of command string*/
  cmdcpy = malloc(sizeof(char) * strlen(trimCmd));
  cmdcpy = strcpy(cmdcpy, trimCmd);

  /*Save a pointer to the mangled string for freeing later*/
  (*stage)->mangled = cmdcpy;
  
  /*Break string on space character, first token should be the name of 
   *the program, and the first argument*/
  arg = strtok(cmdcpy, " ");

  /*If strtok returns null, no ' ' chars were found. Whole line is argument*/
  if( !arg )
  {
    /*Place whole line in arg list*/
    (*stage)->argv[(*stage)->argc++] = trimCmd;
  }
  /*Otherwise a token was generated*/
  else
  {
    /*May need to alloc memory for the argument below*/
    /*Place first arument in arg list*/
    (*stage)->argv[(*stage)->argc++] = arg;

    /*While we are still finding tokens, pass null to search in same string*/
    while( (arg = strtok(NULL, " \n")) )
    {
      /*If an input is to be redirected*/
      if( strcmp(arg, "<") == 0 )
      {
      	/*Grab next token*/
      	arg = strtok(NULL, " \n");

      	/*Previously checked for double input, if next arg is null or output,
      	 *Print the malformed argument and exit*/
      	if( !arg || !strcmp(arg, ">") )
      	{
      	  fprintf(stderr, "%s: bad input redirection\n", trimCmd);
      	  return 0;
      	}
      	/*If next argument is valid*/
      	else
      	{
      	  /*Mark input redirection as to a file*/
      	  (*stage)->redirect[0] = 1;
      	  
      	  /*Save it to the output field*/
      	  (*stage)->file[0] = arg;
      	}
      }
      /*If an output is to be redirected*/
      else if( strcmp(arg, ">") == 0 )
      {
	/*Grab next token*/
	arg = strtok(NULL, " \n");

	/*Previously checked for double output, if next arg is null or input,
	 *Print the malformed argument and exit*/
	if( !arg || !strcmp(arg, "<") )
	{
	  fprintf(stderr, "%s: bad output redirection\n", trimCmd);
	  return 0;
	}
	/*If next argument is valid*/
	else
	{
	  /*Mark output redirection as to a file*/
	  (*stage)->redirect[1] = 1;
	  
	  /*Save it to the output field*/
	  (*stage)->file[1] = arg;
	}
      }
      /*If token isn't an input/output, its an argument*/
      else
      {
      	/*Alloc memory for one more argument and an extra for null*/
      	(*stage)->argv =
      	  realloc((*stage)->argv, sizeof(char*) * (2 + (*stage)->argc));
      	
      	/*Place argument in arg list as next argument*/
      	(*stage)->argv[(*stage)->argc++] = arg;
      }
    }
  }

  /*Null terminate the argv list for execution*/
  (*stage)->argv[(*stage)->argc] = NULL;
  
  /*Return the structure after filling out fields*/

  
  return 1;
}
