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

typedef struct
{
    char *question;
    int answer;
} Question;

struct Message
{
    long mtype;
    char mguess;
};

struct sharedData
{
    int firstPlayerAnswer;
    int secPlayerAnswer;
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
    Ld. fork_sigaction.c referencia: signal() nem hordozható,
    sigaction() garantáltan POSIX-kompatibilis.
*/
void starthandler(int sig)
{
    if (sig == Alarcot_FEL)
    {
        ready++;
    }
}

void generateQuestions(Question *questionArray[], const char *questionSentences[], int arraySize)
{
    srand(time(NULL));
    for (int i = 0; i < arraySize; i++)
    {
        questionArray[i] = malloc(sizeof(Question));
        if (!questionArray[i])
        {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        questionArray[i]->question = strdup(questionSentences[i]);
        if (!questionArray[i]->question)
        {
            perror("strdup");
            exit(EXIT_FAILURE);
        }
        questionArray[i]->answer = rand() % 5 + 1;
    }
}

char *evaluate(int playerAnswer, int goodAnswer)
{
    char *result = malloc(2 * sizeof(char));
    if (!result)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    if (playerAnswer == goodAnswer)
    {
        printf("Valaszod: %u, Mikulast kapsz!\n", playerAnswer);
        result[0] = 'j';
    }
    else
    {
        printf("Valaszod: %u, Virgacsot kapsz!\n", playerAnswer);
        result[0] = 'h';
    }
    result[1] = '\0';
    return result;
}

int rand_id(int max)
{
    return rand() % max;
}

pid_t firstPlayer(int pipe_id_rec, int pipe_id_send)
{
    pid_t process = fork();
    if (process == -1)
    {
        perror("fork (firstPlayer)");
        exit(EXIT_FAILURE);
    }
    if (process > 0)
    {
        return process;
    }

    /* ---- GYEREK FOLYAMAT ---- */

    /*
        Örökölt signal maszk resetelése — ld. fork_sigaction.c referencia.
        A szülő blokkolta a SIGUSR1-et sigsuspend előtt,
        a gyerek ezt örökli, ezért ki kell törölni.
    */
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    kill(mainProcessValue, Alarcot_FEL);

    char randomNames[][10] = {"Tigris", "Oroszlan", "Leopard"};

    srand(getpid());
    int r = rand() % 3;
    write(pipe_id_send, &randomNames[r], 10);

    char question[50];
    read(pipe_id_rec, &question, sizeof(question));

    int answer = rand() % 5 + 1;
    printf("Elso jatekos: Kapott kerdes %s es erre a valaszom: %i\n", question, answer);
    write(pipe_id_send, &answer, sizeof(answer));

    semaphoreOperation(semid, -1);
    s->firstPlayerAnswer = answer;
    semaphoreOperation(semid, 1);

    if (shmdt(s) == -1)
        perror("shmdt (firstPlayer)");

    exit(EXIT_SUCCESS);
}

pid_t secPlayer(int pipe_id_rec, int pipe_id_send)
{
    pid_t process = fork();
    if (process == -1)
    {
        perror("fork (secPlayer)");
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

    kill(mainProcessValue, Alarcot_FEL);

    char randomNames[][10] = {"Malac", "Szamar", "Zebra"};

    srand(getpid());
    int r = rand() % 3;
    write(pipe_id_send, &randomNames[r], 10);

    char question[50];
    read(pipe_id_rec, &question, sizeof(question));

    int answer = rand() % 5 + 1;
    printf("Masodik jatekos: Kapott kerdes %s es erre a valaszom %i\n", question, answer);
    write(pipe_id_send, &answer, sizeof(answer));

    char puffer;
    puffer = rand_id(100) < 50 ? 'h' : 'j';
    int status;
    struct Message ms = {5, puffer};
    status = msgsnd(messageQueue, &ms, sizeof(char), 0);
    if (status < 0)
    {
        perror("msgsnd (secPlayer)");
    }

    semaphoreOperation(semid, -1);
    s->secPlayerAnswer = answer;
    semaphoreOperation(semid, 1);

    if (shmdt(s) == -1)
        perror("shmdt (secPlayer)");

    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc < 1 || argv[0] == NULL)
    {
        fprintf(stderr, "argv[0] nem elérhető\n");
        return EXIT_FAILURE;
    }

    mainProcessValue = getpid();

    /*
        sigaction() használata — ld. fork_sigaction.c referencia.
        signal() viselkedése nem hordozható (egyes rendszereken a handler
        egyszer fut le, aztán visszaáll SIG_DFL-re). sigaction() garantáltan
        POSIX-kompatibilis és a handler megmarad.
    */
    struct sigaction sa;
    sa.sa_handler = starthandler;
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

    int status;
    key_t mainKey;

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

    s = shmat(sh_mem_id, NULL, 0);
    if (s == (void *)-1)
    {
        perror("shmat");
        shmctl(sh_mem_id, IPC_RMID, NULL);
        return EXIT_FAILURE;
    }

    /*
        A szemafor kulcsa '2' projekt-azonosítóval, hogy ne ütközzön
        a message queue / shared memory kulcsával (azok '1'-et használnak).
    */
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

    pid_t child1_pid = firstPlayer(io_pipes[0], io_pipes1[1]);
    pid_t child2_pid = secPlayer(io_pipes2[0], io_pipes3[1]);

    /*
        sigsuspend() használata busy-wait helyett.

        NEM SZÉP:
            while (ready < 1);   // <-- CPU-t pazarol, race condition
            while (ready < 2);

        SZÉP — ld. fork_sigaction.c referencia:
        sigsuspend(&oldmask) ideiglenesen feloldja a SIGUSR1 blokkolását
        és atomikusan várakozik — nincs CPU pazarlás, nincs race condition.
    */
    while (ready < 2)
    {
        sigsuspend(&oldmask);
    }

    /* Eredeti signal maszk visszaállítása */
    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    puts("Alarc fenn! - 1");
    puts("Alarc fenn! - 2");

    char firstPlayerName[10];
    read(io_pipes1[0], &firstPlayerName, sizeof(firstPlayerName));
    printf("Jatekvezeto hangosan mondja: %s\n", firstPlayerName);

    char secPlayerName[10];
    read(io_pipes3[0], &secPlayerName, sizeof(secPlayerName));
    printf("Jatekvezeto hangosan mondja: %s\n", secPlayerName);

    const char *questionSentences[] = {
        "Question sentence 1",
        "Question sentence 2",
        "Question sentence 3",
    };

    int N = 3;
    Question *questions[N];
    generateQuestions(questions, questionSentences, N);

    srand(time(NULL));
    int randomQuestion = rand() % 3;
    printf("Kerdes adatok: %s, %i, %li\n", questions[randomQuestion]->question, questions[randomQuestion]->answer, strlen(questions[randomQuestion]->question));
    write(io_pipes[1], questions[randomQuestion]->question, strlen(questions[randomQuestion]->question) + 1);
    write(io_pipes2[1], questions[randomQuestion]->question, strlen(questions[randomQuestion]->question) + 1);

    int firstAnswer;
    int secondAnswer;
    char *firstResult;
    char *secResult;

    read(io_pipes1[0], &firstAnswer, sizeof(int));
    printf("Elso jatekos valsza: %i\n", firstAnswer);
    firstResult = evaluate(firstAnswer, questions[randomQuestion]->answer);
    read(io_pipes3[0], &secondAnswer, sizeof(int));
    printf("Masodik jatekos valsza: %i\n", secondAnswer);
    secResult = evaluate(secondAnswer, questions[randomQuestion]->answer);

    printf("Elso jatekos valasza ismetelen a kovetkezo: (jo / hamis) -> %s\n", firstResult);

    char *guessValue = malloc(2 * sizeof(char));
    if (!guessValue)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    guessValue[0] = '\0';
    guessValue[1] = '\0';

    struct Message ms;
    status = msgrcv(messageQueue, &ms, sizeof(char), 5, 0);
    if (status < 0)
    {
        perror("msgrcv");
    }
    else
    {
        guessValue[0] = ms.mguess;
        guessValue[1] = '\0';
        printf("A kapott tipp a MASODIK jatekostol az elsore: %c\n", ms.mguess);
    }

    if (strcmp(firstResult, guessValue) == 0)
    {
        printf("A masodik jatekos sikeres valaszt adott! Jo valasz, mikulast kap!\n");
    }
    else
    {
        printf("A masodik jatekos nem jo valaszt adott! Rossz valasz, virgacsot kap!\n");
    }

    semaphoreOperation(semid, -1);
    printf("Elso jatekos leadott valasza: %i\n", s->firstPlayerAnswer);
    printf("Masodik jatekos leadott valasza: %i\n", s->secPlayerAnswer);
    semaphoreOperation(semid, 1);

    if (shmdt(s) == -1)
        perror("shmdt (main)");

    /*
        waitpid EINTR kezelés — ld. osztmem_sec.c, fork_sigaction.c referencia.
        Ha egy signal megszakítja a waitpid()-et, errno == EINTR,
        ilyenkor újra kell próbálni.
    */
    int child_status;
    pid_t finished;

    do {
        finished = waitpid(child1_pid, &child_status, 0);
    } while (finished == -1 && errno == EINTR);

    if (finished == -1)
        perror("waitpid (child1)");
    else if (WIFEXITED(child_status))
        printf("Elso jatekos (PID %d) - kilépett, státusz: %d\n", finished, WEXITSTATUS(child_status));
    else if (WIFSIGNALED(child_status))
        printf("Elso jatekos (PID %d) - signallal megölve: %d\n", finished, WTERMSIG(child_status));

    do {
        finished = waitpid(child2_pid, &child_status, 0);
    } while (finished == -1 && errno == EINTR);

    if (finished == -1)
        perror("waitpid (child2)");
    else if (WIFEXITED(child_status))
        printf("Masodik jatekos (PID %d) - kilépett, státusz: %d\n", finished, WEXITSTATUS(child_status));
    else if (WIFSIGNALED(child_status))
        printf("Masodik jatekos (PID %d) - signallal megölve: %d\n", finished, WTERMSIG(child_status));

    /* Memória felszabadítás */
    for (int i = 0; i < N; i++)
    {
        free(questions[i]->question);
        questions[i]->question = NULL;
        free(questions[i]);
        questions[i] = NULL;
    }
    free(firstResult);
    firstResult = NULL;
    free(secResult);
    secResult = NULL;
    free(guessValue);
    guessValue = NULL;

    /* Pipe-ok lezárása */
    close(io_pipes[0]);
    close(io_pipes[1]);
    close(io_pipes1[0]);
    close(io_pipes1[1]);
    close(io_pipes2[0]);
    close(io_pipes2[1]);
    close(io_pipes3[0]);
    close(io_pipes3[1]);

    /* IPC erőforrások törlése — ld. szemafor.c, msg-queue.c referencia */
    status = msgctl(messageQueue, IPC_RMID, NULL);
    if (status < 0)
        perror("msgctl");

    status = shmctl(sh_mem_id, IPC_RMID, NULL);
    if (status < 0)
        perror("shmctl");

    semaphoreDelete(semid);

    return EXIT_SUCCESS;
}