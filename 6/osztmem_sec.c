#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

char *s;
pid_t mainszalertek = 0;

/*
    Child process létrehozása
*/
pid_t child1()
{
    pid_t szal = fork();

    if (szal == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (szal > 0)
    {
        // Parent visszakapja a child PID-et
        return szal;
    }

    /*
        ---- CHILD PROCESS ----
    */
    printf("A gyerek ezt olvasta az osztott memoriabol: %s", s);
    fflush(stdout);

    // gyerek is elengedi
    if (shmdt(s) == -1)
    {
        perror("shmdt (child)");
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    int status;
    pid_t child_pid;

    key_t kulcs;
    int oszt_mem_id;

    // a parancs nevevel es az 1 verzio szammal kulcs generalas
    kulcs = ftok(argv[0], 1);
    if (kulcs == -1)
    {
        perror("ftok");
        return 1;
    }

    // osztott memoria letrehozasa, irasra olvasasra, 500 bajt merettel
    oszt_mem_id = shmget(kulcs, 500, IPC_CREAT | S_IRUSR | S_IWUSR);
    if (oszt_mem_id < 0)
    {
        perror("shmget");
        return 1;
    }

    // csatlakozunk az osztott memoriahoz,
    // a 2. parameter akkor kell, ha sajat cimhez akarjuk illeszteni
    // a 3. paraméter lehet SHM_RDONLY, ekkor csak read van
    s = shmat(oszt_mem_id, NULL, 0);
    if (s == (void *)-1)
    {
        perror("shmat");
        return 1;
    }

    mainszalertek = getpid();

    char buffer[] = "Valami iras! \n";

    /*
        Hibás verzió:
            fork();
            strcpy(s, buffer);

        Probléma:
        - Nem determinisztikus, hogy a child vagy parent fut előbb
        - Ha a child fut előbb → üres / szemét adatot olvas

        Megoldás:
        - Parent ELŐBB ír
        - UTÁNA fork()

        Így garantált a helyes működés
    */

    // beirunk a memoriaba (FORK ELŐTT VAN)
    strcpy(s, buffer);

    /*
        Child létrehozása CSAK EZUTÁN
    */
    child_pid = child1();

    /*
        ---- PARENT PROCESS ----
    */

    // elengedjuk az osztott memoriat
    if (shmdt(s) == -1)
    {
        perror("shmdt (parent)");
        return 1;
    }

    /*
        Megvárjuk a gyereket korrekt módon
    */
    pid_t finished;

    do
    {
        finished = waitpid(child_pid, &status, 0);
    }
    while (finished == -1 && errno == EINTR);

    if (finished == -1)
    {
        perror("waitpid");
        return 1;
    }

    /*
        IPC_RMID - torolni akarjuk a memoriat,
        ekkor nem kell 3. parameter
    */
    if (shmctl(oszt_mem_id, IPC_RMID, NULL) == -1)
    {
        perror("shmctl");
        return 1;
    }

    return 0;
}