#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define MSG_TEXT_SIZE 1024

struct uzenet
{
    long mtype; // ez egy szabadon hasznalhato ertek, pl uzenetek osztalyozasara
    char mtext[MSG_TEXT_SIZE];
};

struct uzenet2
{
    long mtype;
    int marr[5];
};

// sending a message (parent)
int szulo(int uzenetsor)
{
    struct uzenet uz;
    struct uzenet2 uz2 = {5, {1, 2, 3, 4, 5}};
    int status;

    /* Parent elküld egy integer tömböt */

    status = msgsnd(uzenetsor, &uz2, sizeof(uz2.marr), 0);

    if (status < 0)
    {
        perror("msgsnd");
        return 1;
    }

    /* Parent vár egy üzenetet a gyerektől */

    status = msgrcv(uzenetsor, &uz, MSG_TEXT_SIZE, 4, 0);

    if (status < 0)
        perror("msgrcv");
    else
        printf("A kapott uzenet (gyerektol) kodja: %ld, szovege: %s\n",
               uz.mtype, uz.mtext);

    return 0;
}

// receiving a message (child)
int gyerek(int uzenetsor)
{
    struct uzenet uz = {4, "Child uzenete a szulonek"};
    struct uzenet2 uz2;
    int status;

    /* Child küld egy string üzenetet */

    status = msgsnd(uzenetsor, &uz, strlen(uz.mtext) + 1, 0);

    if (status < 0)
    {
        perror("msgsnd");
        return 1;
    }

    /* Child vár egy tömböt a szülőtől */

    status = msgrcv(uzenetsor, &uz2, sizeof(uz2.marr), 5, 0);

    if (status < 0)
        perror("msgrcv");
    else
    {
        printf("A kapott uzenet (szulotol) kodja: %ld\n", uz2.mtype);

        for (int i = 0; i < sizeof(uz2.marr) / sizeof(int); ++i)
        {
            printf("%d ", uz2.marr[i]);
        }

        printf("\n");
    }

    return 0;
}

int main(int argc, char *argv[])
{
    pid_t child;
    int uzenetsor;
    key_t kulcs;

    /* kulcs generálása ftok segítségével */

    kulcs = ftok(".", 1);

    printf("A kulcs: %d\n", kulcs);

    /* message queue létrehozása */

    uzenetsor = msgget(kulcs, 0600 | IPC_CREAT);

    if (uzenetsor < 0)
    {
        perror("msgget");
        return 1;
    }

    /* process létrehozása */

    child = fork();

    if (child > 0)
    {
        /* ---- PARENT PROCESS ---- */

        szulo(uzenetsor);

        /* child befejezésének korrekt kezelése */

        int child_status;
        pid_t finished;

        do
        {
            finished = waitpid(child, &child_status, 0);
        }
        while (finished == -1 && errno == EINTR);

        if (finished == -1)
        {
            perror("waitpid");
            exit(EXIT_FAILURE);
        }

        if (WIFEXITED(child_status))
            printf("Child exited with code %d\n", WEXITSTATUS(child_status));
        else if (WIFSIGNALED(child_status))
            printf("Child killed by signal %d\n", WTERMSIG(child_status));

        /* message queue törlése */

        int status = msgctl(uzenetsor, IPC_RMID, NULL);

        if (status < 0)
            perror("msgctl");

        printf("Message queue torolve.\n");

        return 0;
    }
    else if (child == 0)
    {
        /* ---- CHILD PROCESS ---- */

        return gyerek(uzenetsor);
    }
    else
    {
        perror("fork");
        return 1;
    }

    return 0;
}