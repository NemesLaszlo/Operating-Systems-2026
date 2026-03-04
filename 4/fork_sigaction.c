#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>

pid_t mainProcessValue;

/*
Since SIGUSR1 and SIGUSR2 are NOT queued signals, if two children send the
SAME signal very quickly, only ONE may remain pending — the kernel stores
only a single pending bit per signal number (POSIX standard behavior).

child1 sends SIGUSR1, child2 sends SIGUSR2 — two DIFFERENT signals.
Different signals each have their own pending bit, so neither can be lost.

We still use a global counter of type sig_atomic_t, which is safe to
modify inside a signal handler.

sig_atomic_t guarantees atomic read/write operations when used in signals.
*/
volatile sig_atomic_t ready_count = 0;

void signal_handler(int sig) {
    // Empty handler to wake up sigsuspend.
    // We increment a counter so we can track how many children are ready.
    ready_count++;
}

pid_t child1() {
    pid_t process = fork();
    if (process == -1)
        exit(EXIT_FAILURE);
    if (process > 0)
        return process;

    // reset inherited blocked signal mask from parent
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    // write() is async-signal-safe and has no stdio lock — safe after fork()
    write(STDOUT_FILENO, "Child1 ready!\n", 14);
    // child1 sends SIGUSR1
    kill(mainProcessValue, SIGUSR1);

    exit(EXIT_SUCCESS);
}

pid_t child2() {
    pid_t process = fork();
    if (process == -1)
        exit(EXIT_FAILURE);
    if (process > 0)
        return process;

    // reset inherited blocked signal mask from parent
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    // write() is async-signal-safe and has no stdio lock — safe after fork()
    write(STDOUT_FILENO, "Child2 ready!\n", 14);
    // child2 sends SIGUSR2 — a different signal than child1.
    // Two identical signals arriving simultaneously collapse into one (POSIX).
    // Two different signals each have their own pending bit — neither is lost.
    kill(mainProcessValue, SIGUSR2);

    exit(EXIT_SUCCESS);
}

int main() {
    mainProcessValue = getpid();

    // We define a sigaction structure named sa, which specifies how the process
    // should handle both SIGUSR1 and SIGUSR2 signals with the same handler.
    struct sigaction sa;
    sa.sa_handler = signal_handler; // Sets signal_handler as the function to handle SIGUSR1 and SIGUSR2
    sigemptyset(&sa.sa_mask);
    // block BOTH signals during handler execution.
    // If SIGUSR1 and SIGUSR2 arrive simultaneously, the first handler runs
    // while the second signal stays pending — guaranteed to be delivered
    // separately after the handler returns. Without this, the second signal
    // could interrupt the first handler, causing ready_count to be incremented
    // twice before sigsuspend() returns, making the second sigsuspend() call
    // wait forever for a signal that already fired.
    sigaddset(&sa.sa_mask, SIGUSR1);
    sigaddset(&sa.sa_mask, SIGUSR2);
    sa.sa_flags = 0; // No special flags are used.
    sigaction(SIGUSR1, &sa, NULL); // Registers sa as the new signal handler for SIGUSR1.
    sigaction(SIGUSR2, &sa, NULL); // Registers sa as the new signal handler for SIGUSR2.

    // Creates two signal sets:
    // - mask (which will contain SIGUSR1 and SIGUSR2)
    // - oldmask (to store the previous signal mask)
    // mask contains SIGUSR1 and SIGUSR2 (which we blocked).
    // oldmask contains the previous signal mask (which did not block them).
    sigset_t mask, oldmask;
    sigemptyset(&mask); // Initializes mask to be empty.
    sigaddset(&mask, SIGUSR1); // Adds SIGUSR1 to mask, meaning we want to block it.
    sigaddset(&mask, SIGUSR2); // Adds SIGUSR2 to mask, meaning we want to block it.
    // -> Ensures that the signals aren't accidentally handled before we call sigsuspend().
    // We only want to process signals when we are explicitly waiting for them.
    // Blocks SIGUSR1 and SIGUSR2, preventing them from being handled until we're ready.
    // Saves the previous signal mask in oldmask, so we can restore it later.
    sigprocmask(SIG_BLOCK, &mask, &oldmask); // Blocks SIGUSR1+SIGUSR2 so they cannot be delivered yet. Saves the old signal mask in oldmask.
    // a folyamat mostantól blokkolja a SIGUSR1-et és SIGUSR2-t, minden érkezésük pending lesz, a handler nem fut le.

    pid_t c1 = child1();
    pid_t c2 = child2();

    /*
    Temporarily unblocks SIGUSR1 and SIGUSR2 and pauses execution until a signal arrives.
    Loop until ready_count reaches 2 to avoid lost signals.
    Since child1 sends SIGUSR1 and child2 sends SIGUSR2, they have separate
    pending bits — both are guaranteed to be delivered exactly once.
    */
    while (ready_count < 2) {
        sigsuspend(&oldmask);
        // -> &oldmask IMPORTANT -> Temporarily restores the signal mask to oldmask (which means SIGUSR1 and SIGUSR2 are no longer blocked).
        // sigsuspend ideiglenesen visszaállítja a maszkot oldmask-ra, ami nem blokkolja a SIGUSR1-et és SIGUSR2-t,
        // így amikor a jel megérkezik, a handler lefut (ready_count++), majd a sigsuspend visszatér.
    }

    write(STDOUT_FILENO, "Child1 signal received!\n", 24);
    write(STDOUT_FILENO, "Child2 signal received!\n", 24);

    // Restore original signal mask
    // Restores the original signal mask (the one before we blocked SIGUSR1 and SIGUSR2).
    sigprocmask(SIG_SETMASK, &oldmask, NULL);

    write(STDOUT_FILENO, "Both children ready! Proceeding...\n", 35);

    // Wait for first child
    int status1;
    // pid_t waitpid(pid_t pid, int *status, int options);
    pid_t finished1;
    // retry waitpid on EINTR — a late signal could interrupt it
    do {
        finished1 = waitpid(c1, &status1, 0);
    } while (finished1 == -1 && errno == EINTR);
    // 0 → Default behavior (waits until the child terminates).
    // WNOHANG → Returns immediately if no child has exited.
    // WUNTRACED → Returns if a child process is stopped.
    // WCONTINUED → Returns if a child continues after SIGCONT.

    if (WIFEXITED(status1)) {
        printf("Child1 (PID %d) exited with status %d\n", finished1, WEXITSTATUS(status1));
    }

    // Wait for second child
    int status2;
    // retry waitpid on EINTR — a late signal could interrupt it
    pid_t finished2;
    do {
        finished2 = waitpid(c2, &status2, 0);
    } while (finished2 == -1 && errno == EINTR);

    if (WIFEXITED(status2)) {
        printf("Child2 (PID %d) exited with status %d\n", finished2, WEXITSTATUS(status2));
    }

    printf("Done\n");

    return 0;
}

/*
1. mask = {SIGUSR1, SIGUSR2}             -> blokkolva
2. oldmask = {}                          -> korábbi állapot, egyik sincs blokkolva
3. sigprocmask(SIG_BLOCK, mask, oldmask) -> most mindkettő blokkolva, pending lesz
4. while (ready_count < 2)
   -> sigsuspend(&oldmask)
   -> ideiglenesen oldmask aktív (SIGUSR1 és SIGUSR2 nincs blokkolva)
   -> SIGUSR1 vagy SIGUSR2 érkezik -> handler fut -> ready_count++
   -> sigsuspend visszatér
   -> maszk visszaáll (mindkettő blokkolva)
   -> ciklus addig ismétlődik, amíg mindkét jel meg nem érkezik
*/