#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>     // fork
#include <sys/wait.h>   // waitpid
#include <errno.h>

int notacommonvalue = 1;
pid_t mainszalertek = 0;

pid_t child1()
{
    pid_t szal = fork();

    if (szal == -1)
    {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (szal > 0)
    {
        // Parent returns child's PID
        return szal;
    }

    // Child process
    notacommonvalue = 5;
    printf("The value is %i in child process\n", notacommonvalue);
    exit(0);
}

pid_t child2()
{
    pid_t szal = fork();

    if (szal == -1)
    {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (szal > 0)
    {
        return szal;
    }

    // Child process
    notacommonvalue = 16;
    printf("The value is %i in child2 process\n", notacommonvalue);
    exit(0);
}

int main()
{
    int status;

    notacommonvalue = 1;
    printf("The value is %i before forking\n", notacommonvalue);

    mainszalertek = getpid();

    // Store returned child PIDs
    pid_t child1_pid = child1();
    pid_t child2_pid = child2();

    // Wait for child1
    if (waitpid(child1_pid, &status, 0) == -1)
    {
        perror("waitpid child1 failed");
    }
    else
    {
        if (WIFEXITED(status))
        {
            printf("Child1 terminated with exit status: %d\n",
                   WEXITSTATUS(status));
        }
        else
        {
            printf("Child1 terminated abnormally\n");
        }
    }

    // Wait for child2
    if (waitpid(child2_pid, &status, 0) == -1)
    {
        perror("waitpid child2 failed");
    }
    else
    {
        if (WIFEXITED(status))
        {
            printf("Child2 terminated with exit status: %d\n",
                   WEXITSTATUS(status));
        }
        else
        {
            printf("Child2 terminated abnormally\n");
        }
    }

    printf("The value is %i in parent process (remain the original)\n",
           notacommonvalue);

    return 0;
}