#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>   //fork
#include <sys/wait.h> //waitpid
#include <errno.h>

int main()
{
   int status;

   pid_t child = fork(); //forks make a copy of variables
   if (child < 0)
   {
      perror("The fork calling was not succesful\n");
      exit(1);
   }
   if (child > 0) //the parent process, it can see the returning value of fork - the child variable!
   {
      waitpid(child, &status, 0);
      printf("The end of parent process\n");
   }
   else //child process
   {
      //to start a program, like printing out a string (in parameter) 5 times (parameter)
      char *cmd = "./write";
      char *arg[] = {"./write", "Operating Systems", "5", NULL}; // needs to be NULL terminated so that exec actually knows when there are no more arguments - IMPORTANT
      printf("./write program will be called by execv function\n");
	  // child process that runs another program with exec
	  // exec is a family of function calls available an any POSIX (Portable Operating System Interface for uniX) compliant operating system,
	  // that essentially runs a new program on the top of current process
	  // runs the program that you specify but replaces the current process (it has the same process id, it still the same process, but it replaced by the new program)
	  //	  "v" (vector) - passed in the arguments to the program as a vector or basically as an array (Passed as an array of strings) - Best when arguments are dynamically determined at runtime
	  // exec    "p" (path) to do path searching - it is gonna look at the path enviroment variable and actually search for the program that I am trying to run (to execute a program without specifying its full path)
	  //	  "l" (list) - it is going to be part of the argument list, so I am gonna list it on the list of the arguments (Explicitly listed as separate arguments) - Best for when the number of arguments is fixed. Less flexible, must specify each argument manually
	  // execvp"e" - means you want to pass a different set of enviroment variables to the new program (without "e" enviroment variables from the original will just passed to the new process)
      execv(cmd, arg);
      printf("It never returns to child process (except on error)- the content of the whole child process will be changed to the new one");
   }
   return 0;
}