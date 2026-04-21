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

struct Message
{
    long mtype;
    char mtext[1024];
};

pid_t mainProcessValue = 0;

/*
    sig_atomic_t + volatile: biztonságos signal handlerből való írás/olvasás.
    Ld. fork_sigaction.c referencia.
*/
volatile sig_atomic_t ready = 0;

int messageQueue;
int semid;

/* ---- Szemafor segédfüggvények (ld. szemafor.c referencia) ---- */

int semaphoreCreation(const char *pathname, int semaphoreValue)
{
    int semid;
    key_t key;

    /*
        '2' projekt-azonosító, hogy ne ütközzön
        a message queue kulcsával (az '1'-et használ).
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
void starthandler(int sig)
{
    if (sig == SIGUSR1)
    {
        ready++;
    }
}

pid_t ursula(int pipe_id_rec, int pipe_id_send)
{
    /* A (void) cast jelzi a fordítónak, hogy szándékosan nem használjuk ezt a paramétert,
       így elnyomja az "unused parameter" warningot (-Wextra flag esetén). */
    (void)pipe_id_send;

    pid_t process = fork();
    if (process == -1)
    {
        perror("fork (ursula)");
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

    int toVaccinate;
    read(pipe_id_rec, &toVaccinate, sizeof(int));
    printf("Ursula ennyit fog oltani: %d\n", toVaccinate);

    int vaccinated = 0;

    srand(getpid());

    for (int i = 0; i < toVaccinate; i++)
    {
        int chance = rand() % 100;
        int success = (chance < 80);
        if (success != 0)
        {
            vaccinated++;
        }
    }
    printf("Ursulanak ennyit sikerult beoltani: %d\n", vaccinated);
    printf("Ursula - Betegek, nem lehetett oltani: %d\n", toVaccinate - vaccinated);

    struct Message msg;
    msg.mtype = 5;
    sprintf(msg.mtext, "%d", toVaccinate - vaccinated);
    if (msgsnd(messageQueue, &msg, strlen(msg.mtext) + 1, 0) < 0)
        perror("msgsnd (ursula sick)");

    struct Message msgg;
    msgg.mtype = 6;
    sprintf(msgg.mtext, "%d", vaccinated);
    if (msgsnd(messageQueue, &msgg, strlen(msgg.mtext) + 1, 0) < 0)
        perror("msgsnd (ursula vaccinated)");

    exit(EXIT_SUCCESS);
}

pid_t csormester(int pipe_id_rec, int pipe_id_send)
{
    /* A (void) cast jelzi a fordítónak, hogy szándékosan nem használjuk ezt a paramétert,
       így elnyomja az "unused parameter" warningot (-Wextra flag esetén). */
    (void)pipe_id_send;

    pid_t process = fork();
    if (process == -1)
    {
        perror("fork (csormester)");
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

    int toVaccinate;
    read(pipe_id_rec, &toVaccinate, sizeof(int));
    printf("Csormester ennyit fog oltani: %d\n", toVaccinate);

    int vaccinated2 = 0;

    srand(getpid());

    for (int i = 0; i < toVaccinate; i++)
    {
        int chance2 = rand() % 100;
        int success2 = (chance2 < 80);
        if (success2 != 0)
        {
            vaccinated2++;
        }
    }
    printf("Csormesternek ennyit sikerult beoltani: %d\n", vaccinated2);
    printf("Csormester - Betegek, nem lehetett oltani: %d\n", toVaccinate - vaccinated2);

    struct Message msg;
    msg.mtype = 5;
    sprintf(msg.mtext, "%d", toVaccinate - vaccinated2);
    if (msgsnd(messageQueue, &msg, strlen(msg.mtext) + 1, 0) < 0)
        perror("msgsnd (csormester sick)");

    struct Message msgg;
    msgg.mtype = 6;
    sprintf(msgg.mtext, "%d", vaccinated2);
    if (msgsnd(messageQueue, &msgg, strlen(msgg.mtext) + 1, 0) < 0)
        perror("msgsnd (csormester vaccinated)");

    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        puts("Missing argument!");
        return EXIT_FAILURE;
    }

    if (argv[0] == NULL)
    {
        fprintf(stderr, "argv[0] nem elérhető\n");
        return EXIT_FAILURE;
    }

    int status;
    key_t mainKey;
    mainProcessValue = getpid();

    struct sigaction sa;
    sa.sa_handler = starthandler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGUSR1);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    /*
        Signal maszk: SIGUSR1 blokkolása fork() ELŐTT.
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

    semid = semaphoreCreation(argv[0], 1);

    int patients = atoi(argv[1]);
    int ursulanum = patients / 2;
    int csormesternum = patients - ursulanum;
    printf("ursulanum: %d, csormesternum: %d \n", ursulanum, csormesternum);

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

    int io_pipes2[2];
    if (pipe(io_pipes2) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    int io_pipes3[2];
    if (pipe(io_pipes3) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t child1_pid = ursula(io_pipes[0], io_pipes1[1]);
    pid_t child2_pid = csormester(io_pipes2[0], io_pipes3[1]);

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

    puts("Ursula keszen all az oltasra!");
    puts("Csormester keszen all az oltasra!");

    write(io_pipes[1], &ursulanum, sizeof(int));
    write(io_pipes2[1], &csormesternum, sizeof(int));

    int vaccinatedUrsula;
    int vaccinatedCsor;
    int sickUrsula;
    int sickCsor;

    struct Message msg;

    if (msgrcv(messageQueue, &msg, sizeof(msg.mtext), 5, 0) < 0)
        perror("msgrcv (sick 1)");
    sickUrsula = atoi(msg.mtext);

    if (msgrcv(messageQueue, &msg, sizeof(msg.mtext), 6, 0) < 0)
        perror("msgrcv (vaccinated 1)");
    vaccinatedUrsula = atoi(msg.mtext);

    if (msgrcv(messageQueue, &msg, sizeof(msg.mtext), 5, 0) < 0)
        perror("msgrcv (sick 2)");
    sickCsor = atoi(msg.mtext);

    if (msgrcv(messageQueue, &msg, sizeof(msg.mtext), 6, 0) < 0)
        perror("msgrcv (vaccinated 2)");
    vaccinatedCsor = atoi(msg.mtext);

    printf("Oltottak szama: %d\n", vaccinatedUrsula + vaccinatedCsor);
    printf("Betegek szama: %d\n", sickUrsula + sickCsor);

    semaphoreOperation(semid, -1);
    FILE *temp;
    temp = fopen("data.txt", "a");
    if (temp == NULL)
    {
        fprintf(stderr, "\nError - cannot open the file\n");
        semaphoreOperation(semid, 1);
    }
    else
    {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        fprintf(temp, "Ezen a napon: %d-%02d-%02d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        fprintf(temp, "Oltasra erkezettekbol: %d ennyi kapott oltast:%d, ennyi nem: %d\n",
                patients, vaccinatedUrsula + vaccinatedCsor,
                patients - (vaccinatedUrsula + vaccinatedCsor));
        fclose(temp);
        semaphoreOperation(semid, 1);
    }

    printf("Dr. Bubo befejezi a tevekenyseget.\n");

    int child_status;
    pid_t finished;

    do {
        finished = waitpid(child1_pid, &child_status, 0);
    } while (finished == -1 && errno == EINTR);

    if (finished == -1)
        perror("waitpid (ursula)");
    else if (WIFEXITED(child_status))
        printf("Ursula (PID %d) - kilépett, státusz: %d\n", finished, WEXITSTATUS(child_status));
    else if (WIFSIGNALED(child_status))
        printf("Ursula (PID %d) - signallal megölve: %d\n", finished, WTERMSIG(child_status));

    do {
        finished = waitpid(child2_pid, &child_status, 0);
    } while (finished == -1 && errno == EINTR);

    if (finished == -1)
        perror("waitpid (csormester)");
    else if (WIFEXITED(child_status))
        printf("Csormester (PID %d) - kilépett, státusz: %d\n", finished, WEXITSTATUS(child_status));
    else if (WIFSIGNALED(child_status))
        printf("Csormester (PID %d) - signallal megölve: %d\n", finished, WTERMSIG(child_status));

    /* Pipe-ok lezárása */
    close(io_pipes[0]);
    close(io_pipes[1]);
    close(io_pipes1[0]);
    close(io_pipes1[1]);
    close(io_pipes2[0]);
    close(io_pipes2[1]);
    close(io_pipes3[0]);
    close(io_pipes3[1]);

    /* IPC erőforrások törlése */
    status = msgctl(messageQueue, IPC_RMID, NULL);
    if (status < 0)
        perror("msgctl");

    semaphoreDelete(semid);

    return EXIT_SUCCESS;
}