#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <errno.h>

struct message
{
    long mtype;
    char mtext[1024];
};

struct sharedData
{
    char text[1024];
};

pid_t mainProcessValue = 0;

/*
    sig_atomic_t + volatile: biztonságos signal handlerből való írás/olvasás.
    Ld. fork_sigaction.c referencia.
*/
volatile sig_atomic_t ready = 0;

int messageQueue;
int semid;
struct sharedData *s;

/* ---- Szemafor segédfüggvények (ld. szemafor.c referencia) ---- */

int semaphoreCreation(const char *pathname, int semaphoreValue)
{
    int semid;
    key_t key;

    /*
        '2' projekt-azonosító, hogy ne ütközzön
        a message queue / shared memory kulcsával (azok '1'-et használnak).
        Ld. szemafor.c referencia.
    */
    key = ftok(pathname, 2);
    if (key == -1)
    {
        perror("ftok (semaphore)");
        exit(EXIT_FAILURE);
    }

    semid = semget(key, 1, IPC_CREAT | S_IRUSR | S_IWUSR);
    if (semid == -1)
    {
        perror("semget");
        exit(EXIT_FAILURE);
    }

    if (semctl(semid, 0, SETVAL, semaphoreValue) == -1)
    {
        perror("semctl");
        exit(EXIT_FAILURE);
    }

    return semid;
}

void semaphoreOperation(int semid, int op)
{
    struct sembuf operation;

    operation.sem_num = 0;
    operation.sem_op = op;
    operation.sem_flg = 0;

    if (semop(semid, &operation, 1) == -1)
    {
        perror("semop");
        exit(EXIT_FAILURE);
    }
}

void semaphoreDelete(int semid)
{
    if (semctl(semid, 0, IPC_RMID) == -1)
        perror("semctl (IPC_RMID)");
}

/*
    Signal handler — sigaction-nal regisztrálva (nem signal()-lal).
    Ld. fork_sigaction.c referencia.
*/
void readyHandler(int sig)
{
    if (sig == SIGUSR1)
    {
        ready++;
    }
}

pid_t expert(int pipe_id_rec, int pipe_id_send)
{
    pid_t process = fork();
    if (process == -1)
    {
        perror("fork (expert)");
        exit(EXIT_FAILURE);
    }
    if (process > 0)
    {
        return process;
    }

    /* ---- GYEREK FOLYAMAT ---- */

    /* Örökölt signal maszk resetelése — ld. fork_sigaction.c referencia */
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    kill(mainProcessValue, SIGUSR1);

    char puffer[27];
    read(pipe_id_rec, puffer, sizeof(puffer));
    printf("expert - Kapott Kérdés: %s\n", puffer);
    write(pipe_id_send, "Igen", 5);

    exit(EXIT_SUCCESS);
}

pid_t spokesman()
{
    pid_t process = fork();
    if (process == -1)
    {
        perror("fork (spokesman)");
        exit(EXIT_FAILURE);
    }
    if (process > 0)
    {
        return process;
    }

    /* ---- GYEREK FOLYAMAT ---- */

    /* Örökölt signal maszk resetelése */
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    kill(mainProcessValue, SIGUSR1);

    int status;
    struct message ms = {5, "Igen méréseink vannak, amiket publikálni fogunk."};
    status = msgsnd(messageQueue, &ms, strlen(ms.mtext) + 1, 0);
    if (status < 0)
    {
        perror("msgsnd");
    }

    char newData[50] = "20%";
    semaphoreOperation(semid, -1);
    strcpy(s->text, newData);
    semaphoreOperation(semid, 1);

    if (shmdt(s) == -1)
        perror("shmdt (spokesman)");

    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc < 1 || argv[0] == NULL)
    {
        fprintf(stderr, "argv[0] nem elérhető\n");
        return EXIT_FAILURE;
    }

    int status;
    key_t mainKey;
    mainProcessValue = getpid();

    struct sigaction sa;
    sa.sa_handler = readyHandler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGUSR1); /* handler futása közben blokkolja SIGUSR1-et */
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    /*
        Signal maszk: SIGUSR1 blokkolása fork() ELŐTT,
        hogy ne vesszen el a signal a fork és a sigsuspend között.
        Ld. fork_sigaction.c és msg-queue.c referencia.
    */
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    mainKey = ftok(argv[0], 1);
    if (mainKey == -1)
    {
        perror("ftok");
        return EXIT_FAILURE;
    }

    messageQueue = msgget(mainKey, 0600 | IPC_CREAT);
    if (messageQueue < 0)
    {
        perror("msgget");
        return EXIT_FAILURE;
    }

    int sh_mem_id;
    sh_mem_id = shmget(mainKey, sizeof(struct sharedData), IPC_CREAT | S_IRUSR | S_IWUSR);
    if (sh_mem_id == -1)
    {
        perror("shmget");
        return EXIT_FAILURE;
    }

    /*
        shmat() — csatolás. Hiba esetén (void *)-1-et ad vissza, NEM NULL-t!
        Ld. osztmem_sec.c referencia.
    */
    s = shmat(sh_mem_id, NULL, 0);
    if (s == (void *)-1)
    {
        perror("shmat");
        shmctl(sh_mem_id, IPC_RMID, NULL);
        return EXIT_FAILURE;
    }

    semid = semaphoreCreation(argv[0], 1);

    int io_pipes[2];
    if (pipe(io_pipes) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    int io_pipes1[2];
    if (pipe(io_pipes1) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t child1_pid = expert(io_pipes1[0], io_pipes[1]);
    pid_t child2_pid = spokesman();

    /*
        sigsuspend() használata busy-wait helyett.
        Ld. fork_sigaction.c referencia.
    */
    while (ready < 2)
    {
        sigsuspend(&oldmask);
    }

    /* Eredeti signal maszk visszaállítása */
    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    puts("expert kész!");
    puts("spokesman kész!");

    char puffer[5];
    /*
        strlen("Megvan minden dokumentuma?") = 25, + '\0' = 26 byte.
    */
    const char *question = "Megvan minden dokumentuma?";
    write(io_pipes1[1], question, strlen(question) + 1);
    read(io_pipes[0], puffer, sizeof(puffer));
    printf("A expert válasza: %s\n", puffer);

    struct message ms;
    status = msgrcv(messageQueue, &ms, 1024, 5, 0);
    if (status < 0)
    {
        perror("msgrcv");
    }
    else
    {
        printf("A kapott üzenet a spokesman-től kodja: %ld, szovege:  %s \n", ms.mtype, ms.mtext);
    }

    semaphoreOperation(semid, -1);
    printf("A spokesman közölt adata: %s\n", s->text);
    semaphoreOperation(semid, 1);

    if (shmdt(s) == -1)
        perror("shmdt (main)");

    int child_status;
    pid_t finished;

    do {
        finished = waitpid(child1_pid, &child_status, 0);
    } while (finished == -1 && errno == EINTR);

    if (finished == -1)
        perror("waitpid (expert)");
    else if (WIFEXITED(child_status))
        printf("expert (PID %d) - kilépett, státusz: %d\n", finished, WEXITSTATUS(child_status));
    else if (WIFSIGNALED(child_status))
        printf("expert (PID %d) - signallal megölve: %d\n", finished, WTERMSIG(child_status));

    do {
        finished = waitpid(child2_pid, &child_status, 0);
    } while (finished == -1 && errno == EINTR);

    if (finished == -1)
        perror("waitpid (spokesman)");
    else if (WIFEXITED(child_status))
        printf("spokesman (PID %d) - kilépett, státusz: %d\n", finished, WEXITSTATUS(child_status));
    else if (WIFSIGNALED(child_status))
        printf("spokesman (PID %d) - signallal megölve: %d\n", finished, WTERMSIG(child_status));

    /* Pipe-ok lezárása */
    close(io_pipes1[0]);
    close(io_pipes1[1]);
    close(io_pipes[0]);
    close(io_pipes[1]);

    /* IPC erőforrások törlése */
    status = msgctl(messageQueue, IPC_RMID, NULL);
    if (status < 0)
        perror("msgctl");

    status = shmctl(sh_mem_id, IPC_RMID, NULL);
    if (status < 0)
        perror("shmctl");

    semaphoreDelete(semid);

    return EXIT_SUCCESS;
}