#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>   // waitpid miatt kell

// Child process létrehozása
pid_t child(int pipe_id_rec, int pipe_id_send)
{
    pid_t szal = fork();

    if (szal == -1)
    {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }

    // Parent visszatér a child PID-del
    if (szal > 0)
    {
        return szal;
    }

    // ---- CHILD PROCESS ----

    int arrived;
    char str[] = "Child kuldi a szulonek: Megkaptam a szamot!";

    // Szám olvasása a pipe-ból
    if (read(pipe_id_rec, &arrived, sizeof(int)) == -1)
    {
        perror("Child read error");
        exit(EXIT_FAILURE);
    }

    printf("Child olvasta uzenet: %i\n", arrived);

    // Válasz küldése a parent-nek
    if (write(pipe_id_send, str, sizeof(str)) == -1)
    {
        perror("Child write error");
        exit(EXIT_FAILURE);
    }

    printf("Child elkuldte a valaszt.\n");

    exit(0);
}

int main()
{
    int io_pipes[2];   // child -> parent kommunikáció
    int io_pipes1[2];  // parent -> child kommunikáció

    if (pipe(io_pipes) == -1)
    {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }

    if (pipe(io_pipes1) == -1)
    {
        perror("Pipe creation failed");
        exit(EXIT_FAILURE);
    }

    char pufferchar[50];

    pid_t child_pid = child(io_pipes1[0], io_pipes[1]); // 0 ban fogad 1 essel kuld

    // ---- PARENT PROCESS ----

    srand(time(NULL));
    int r = rand() % 100;   // random szám 0-99

    printf("Parent random number: %i\n", r);

    // Szám küldése a child-nak
    if (write(io_pipes1[1], &r, sizeof(r)) == -1)
    {
        perror("Parent write error");
        exit(EXIT_FAILURE);
    }

    printf("Szulo beirta az adatot a csobe!\n");

    // Child válaszának olvasása
    if (read(io_pipes[0], pufferchar, sizeof(pufferchar)) == -1)
    {
        perror("Parent read error");
        exit(EXIT_FAILURE);
    }

    printf("Child mondja (Szulo kapta): %s\n", pufferchar);

    int status;

    waitpid(child_pid, &status, 0);

    if (WIFEXITED(status))
        printf("Child exited with code %d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("Child killed by signal %d\n", WTERMSIG(status));

    // Pipe végek lezárása
    close(io_pipes1[0]);
    close(io_pipes1[1]);
    close(io_pipes[0]);
    close(io_pipes[1]);

    return 0;
}