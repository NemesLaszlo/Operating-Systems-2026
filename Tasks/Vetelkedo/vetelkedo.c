#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

#define Alarcot_FEL SIGUSR1


int main(int argc, char **argv)
{
    if (argc < 1 || argv[0] == NULL)
    {
        fprintf(stderr, "argv[0] nem elérhető\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}