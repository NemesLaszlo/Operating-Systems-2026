#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>   //fork
#include <sys/wait.h> //waitpid
#include <errno.h>

pid_t mainszalertek = 0;
int notacommonvalue = 1;

pid_t child()
{
    // a return re szülségünk van
    // mert a fork() a gyereknek 0 val tér vissza, a szülőnek pedig a gyerek pid azonosítójával.

    // fork() returns 0 to the child process and the child's PID to the parent process.
    pid_t szal = fork();
    if (szal == -1)
        exit(-1);
    if (szal > 0)
    {
        return szal; // need that return here.
    }
    notacommonvalue = 5; //it changes the value of the copy of the variable
    printf("The value is %i in child process \n", notacommonvalue);
    // This is the important piece, the child branch must exit.
    //The critical thing is once the child is done doing whatever
    //it's doing the child branch must exit else it will continue along and run the rest of the code.
    exit(0);

    // More about exit(0):
    // The child process must terminate here.
    // After fork(), both parent and child continue execution from
    // the same point. If the child does not exit, it would continue
    // executing the remaining program logic intended for the parent,
    // which can lead to incorrect behavior (e.g. duplicate execution,
    // unexpected waits, or spawning extra processes).
}

int main()
{
    notacommonvalue = 2;
    printf("The value is %i before forking \n", notacommonvalue);

    mainszalertek = getpid();
    child();
    wait(NULL);
    // wait(NULL) waits for termination of any child process.
    // In programs where multiple children may exist, this can cause
    // the parent to continue execution after a different child terminates,
    // potentially leaving the intended child running. Using waitpid()
    // with the returned PID provides deterministic synchronization.
    printf("The value is %i in parent process (remain the original) \n", notacommonvalue);

    return 0;
}