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
#include <sys/stat.h>
#include <errno.h>

/*
msgget(): either returns the message queue identifier for a newly created message 
queue or returns the identifiers for a queue which exists with the same key value.

msgsnd(): Data is placed on to a message queue by calling msgsnd().

msgrcv(): messages are retrieved from a queue.

msgctl(): It performs various operations on a queue. Generally it is use to 
destroy message queue.
*/

#define MSG_SIZE 1024

struct message
{
    long mtype; // ez egy szabadon hasznalhato ertek, pl uzenetek osztalyozasara
    char mtext[MSG_SIZE];
};

pid_t mainProcessValue = 0;
int messageQueue;

/*
sig_atomic_t garantálja, hogy a változó
atomikusan írható/olvasható signal handlerből
*/
volatile sig_atomic_t ready = 0;

/* Signal handler */
void readyHandler(int sig)
{
    if (sig == SIGUSR1)
    {
        ready++;
    }
}

/* Child process létrehozása */
pid_t child1()
{
    pid_t process = fork();

    if (process == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    // Parent visszatér a child PID-del
    if (process > 0)
        return process;

    // ---- CHILD PROCESS ----

    /* reset inherited blocked signal mask from parent */
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    /* jelezzük a parentnek hogy a child elindult */
    kill(mainProcessValue, SIGUSR1);

    int status;

    struct message ms;
    ms.mtype = 5;
    strcpy(ms.mtext, "This is a test sentence.");

    // msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);
    // The third argument, msgsz, is the size of message (the message should end with a null character)

    status = msgsnd(messageQueue, &ms, strlen(ms.mtext) + 1, 0);
    // a 4. parameter gyakran IPC_NOWAIT, ez a 0-val azonos

    /*
    IPC_NOWAIT röviden magyarul:
        Ha az üzenetsor megtelt, akkor az üzenetet nem írja a sorba,
        és a vezérlés visszatér a hívási folyamathoz. Ha nincs megadva,
        akkor a hívás folyamatát felfüggeszti (blokkolja), amíg az üzenet meg nem írható.
    */

    /*
    The argument msgflg specifies the action to be taken if one or more of the following are true:
        Placing the message on the message queue would cause the current number of bytes on the message queue (msg_cbytes) to exceed the maximum number of bytes allowed on this queue, as specified in msg_qbytes.
        The total number of messages on the queue is equal to the system-imposed limit.

    These actions are as follows:
    If the IPC_NOWAIT flag is on in msgflg, the message will not be sent and the calling process will return immediately. msgsnd() will return -1 and set errno to EAGAIN.
    If the IPC_NOWAIT flag is off in msgflg, the calling process will suspend execution until one of the following occurs:
        The condition responsible for the suspension no longer exists, in which case the message is sent.
        The message queue identifier, msgid, is removed from the system; when this occurs, errno is set to EIDRM and a value of -1 is returned.
        The calling process receives a signal that is to be caught; in this case a message is not sent and the calling process resumes execution. A value of -1 is returned and error is set to EINTR.
    */

    if (status < 0)
    {
        perror("msgsnd");
        exit(EXIT_FAILURE);
    }

    write(STDOUT_FILENO, "Child elkuldte az uzenetet.\n", 28);

    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    mainProcessValue = getpid();

    /* sigaction használata signal helyett */

    struct sigaction sa;
    sa.sa_handler = readyHandler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGUSR1); // block SIGUSR1 while handler runs
    sa.sa_flags = 0;

    sigaction(SIGUSR1, &sa, NULL);

    /*
    Létrehozunk két signal maskot:
    mask -> blokkolni fogjuk vele a SIGUSR1-et
    oldmask -> az eredeti mask
    */

    sigset_t mask, oldmask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);

    /*
    blokkoljuk a SIGUSR1-et, hogy ne fusson le
    mielőtt sigsuspend-et hívnánk
    */

    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    int status;
    key_t mainKey;

    // msgget needs a key, amelyet az ftok generál
    // ftok - convert a pathname and a project identifier to a System V key

    mainKey = ftok(".", 1);

    printf("Key: %d\n", mainKey);

    // 0600 represents read and write permissions for the owner of the message queue.
    // IPC_CREAT is a flag that indicates that the message queue should be created if it does not already exist.

    messageQueue = msgget(mainKey, 0600 | IPC_CREAT);

    if (messageQueue < 0)
    {
        perror("msgget");
        return 1;
    }

    pid_t child_pid = child1();

    /*
    Temporarily unblocks SIGUSR1 and waits for signal.
    Avoids race condition that exists with pause().
    */

    while (ready < 1)
    {
        sigsuspend(&oldmask);
    }

    /* restore original mask */

    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    puts("Child1 kesz!");

    struct message ms;

    // msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);

    status = msgrcv(messageQueue, &ms, MSG_SIZE, 5, 0);

    if (status < 0)
    {
        perror("msgrcv");
    }
    else
    {
        printf("A kapott uzenet a child1-tol kodja: %ld, szovege: %s\n",
               ms.mtype, ms.mtext);
    }

    /* Child process befejezésének korrekt kezelése */

    int child_status;
    pid_t finished;

    do
    {
        finished = waitpid(child_pid, &child_status, 0);
    } while (finished == -1 && errno == EINTR);

    if (WIFEXITED(child_status))
        printf("Child exited with code %d\n", WEXITSTATUS(child_status));
    else if (WIFSIGNALED(child_status))
        printf("Child killed by signal %d\n", WTERMSIG(child_status));

    /* Message queue törlése */

    status = msgctl(messageQueue, IPC_RMID, NULL);

    if (status < 0)
    {
        perror("msgctl");
    }

    printf("Message queue torolve.\n");

    return 0;
}