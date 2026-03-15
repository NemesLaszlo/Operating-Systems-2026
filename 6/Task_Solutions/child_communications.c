#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

int rand_num(int max)
{
    return rand() % max;
}

/* ---------- CHILD 1 ---------- */
/* Parent -> Child1 -> Child2 */

pid_t child_1(int pipe_id_rec, int pipe_id_send)
{
    pid_t pid = fork();

    if (pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
        return pid;

    /* ---- CHILD1 ---- */

    int arrived;

    if (read(pipe_id_rec, &arrived, sizeof(int)) == -1)
    {
        perror("Child1 read");
        exit(EXIT_FAILURE);
    }

    printf("Child_1 olvasta uzenet: %i\n", arrived);

    /* továbbküldi a számot child2-nek */

    if (write(pipe_id_send, &arrived, sizeof(int)) == -1)
    {
        perror("Child1 write");
        exit(EXIT_FAILURE);
    }

    srand(time(NULL) ^ getpid());

    for (int i = 0; i < arrived; i++)
    {
        int random_num = rand_num(1000);

        if (write(pipe_id_send, &random_num, sizeof(int)) == -1)
        {
            perror("Child1 write random");
            exit(EXIT_FAILURE);
        }
    }

    close(pipe_id_rec);
    close(pipe_id_send);

    exit(EXIT_SUCCESS);
}

/* ---------- CHILD 2 ---------- */

pid_t child_2(int pipe_id_rec)
{
    pid_t pid = fork();

    if (pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
        return pid;

    /* ---- CHILD2 ---- */

    int arrived;

    if (read(pipe_id_rec, &arrived, sizeof(int)) == -1)
    {
        perror("Child2 read");
        exit(EXIT_FAILURE);
    }

    printf("Child_2 olvasta uzenet: %i\n", arrived);

    int buffer;

    for (int i = 0; i < arrived; i++)
    {
        if (read(pipe_id_rec, &buffer, sizeof(int)) == -1)
        {
            perror("Child2 read random");
            exit(EXIT_FAILURE);
        }

        printf("Child_2 olvasta random szam: %i\n", buffer);
    }

    close(pipe_id_rec);

    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Hasznalat: %s <szam>\n", argv[0]);
        return 1;
    }

    int console_num = atoi(argv[1]);

    srand(time(NULL));

    /* pipe: parent -> child1 */

    int parent_child1[2];

    if (pipe(parent_child1) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    /* pipe: child1 -> child2 */

    int child1_child2[2];

    if (pipe(child1_child2) == -1)
    {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pid_t c1 = child_1(parent_child1[0], child1_child2[1]);
    pid_t c2 = child_2(child1_child2[0]);

    /* ---- PARENT ---- */

    if (write(parent_child1[1], &console_num, sizeof(int)) == -1)
    {
        perror("Parent write");
        exit(EXIT_FAILURE);
    }

    close(parent_child1[0]);
    close(parent_child1[1]);
    close(child1_child2[0]);
    close(child1_child2[1]);

    /* várjuk a gyerekeket */

    int status;
    pid_t finished;

    do
    {
        finished = waitpid(c1, &status, 0);
    }
    while (finished == -1 && errno == EINTR);

    do
    {
        finished = waitpid(c2, &status, 0);
    }
    while (finished == -1 && errno == EINTR);

    printf("Parent befejezte.\n");

    return 0;
}