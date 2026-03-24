#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define MEMSIZE 1024

/*
    sharedData — az osztott memória struktúrája

    Ahelyett, hogy csak egy sima char tömböt osztanánk meg,
    egy struktúrában tároljuk az összes közös adatot.
    Így az osztott memóriában egyszerre több mező is elérhető,
    és könnyen bővíthető új mezőkkel.

    - text:     a szülő által beírt szöveg
    - textSize: a szöveg hossza, amelyet a gyermek számol ki és ír vissza
                → ez a szülő-gyermek kétirányú kommunikáció példája
*/
struct sharedData
{
    char text[MEMSIZE];
    int textSize;
};

/*
    szemafor_letrehozas(), szemafor_muvelet(), szemafor_torles()
    — ld. részletes magyarázat az előző példában.

    KÜLÖNBSÉG az előző példához képest:
    - Kezdőérték itt 1 (nyitott), nem 0 (zárt).
    - Indokás: ez a példa kölcsönös kizárást (mutex) valósít meg,
      nem egyirányú szinkronizációt.
      A szemafor itt azt védi, hogy szülő és gyermek ne egyszerre
      férjen hozzá az osztott memóriához.
      Kezdőérték = 1 → az első down() azonnal továbbenged.
*/
int szemafor_letrehozas(const char *pathname, int szemafor_ertek)
{
    int semid;
    key_t kulcs;

    kulcs = ftok(pathname, 2);
    if (kulcs == -1)
    {
        perror("ftok (szemafor)");
        exit(EXIT_FAILURE);
    }

    // semget 2. parameter is the number of semaphores
    semid = semget(kulcs, 1, IPC_CREAT | S_IRUSR | S_IWUSR);
    if (semid == -1)
    {
        perror("semget");
        exit(EXIT_FAILURE);
    }

    // 0 = first semaphore
    if (semctl(semid, 0, SETVAL, szemafor_ertek) == -1)
    {
        perror("semctl (SETVAL)");
        exit(EXIT_FAILURE);
    }

    return semid;
}

void szemafor_muvelet(int semid, int op)
{
    struct sembuf muvelet;

    muvelet.sem_num = 0;
    muvelet.sem_op  = op; // op=1 up, op=-1 down
    muvelet.sem_flg = 0;

    if (semop(semid, &muvelet, 1) == -1) // 1 number of sem. operations
    {
        perror("semop");
        exit(EXIT_FAILURE);
    }
}

void szemafor_torles(int semid)
{
    if (semctl(semid, 0, IPC_RMID) == -1)
        perror("semctl (IPC_RMID)");
}

int main(int argc, char *argv[])
{
    pid_t child;
    key_t kulcs;
    int sh_mem_id, semid;
    struct sharedData *s;

    if (argc < 1 || argv[0] == NULL)
    {
        fprintf(stderr, "argv[0] nem elérhető\n");
        return EXIT_FAILURE;
    }

    kulcs = ftok(argv[0], 1);
    if (kulcs == -1)
    {
        perror("ftok (memoria)");
        return EXIT_FAILURE;
    }

    sh_mem_id = shmget(kulcs, sizeof(struct sharedData), IPC_CREAT | S_IRUSR | S_IWUSR);
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
        Szemafor kezdőértéke 1 → "nyitott" (mutex mód).

        KÜLÖNBSÉG az előző példától:
        - Előző példa: kezdőérték = 0 → egyirányú szinkronizáció
          (gyermek vár, amíg szülő ír)
        - Ez a példa: kezdőérték = 1 → kölcsönös kizárás (mutex)
          (bármelyik fél először érheti el, de egyszerre csak egy)

        Működés:
        - down(-1): belépés a kritikus szakaszba (érték: 1→0)
        - up(+1):   kilépés a kritikus szakaszból (érték: 0→1)
        - Ha az egyik fél bent van, a másik down()-ja blokkolódik
    */
    semid = szemafor_letrehozas(argv[0], 1);

    child = fork();
    if (child == -1)
    {
        perror("fork");
        shmdt(s);
        shmctl(sh_mem_id, IPC_RMID, NULL);
        szemafor_torles(semid);
        return EXIT_FAILURE;
    }

    if (child > 0)
    {
        /*
            ---- SZÜLŐ FOLYAMAT — billentyűzetről olvas, osztott memóriába ír ----

            Ciklus:
            1. Beolvas egy sort stdin-ről (fgets)
            2. Szemafor down → kritikus szakasz
            3. Beír az osztott memóriába (text mező)
            4. Kiolvassa a gyermek által kiszámolt textSize-t (kétirányú kommunikáció)
            5. Szemafor up → kritikus szakasz vége
            6. Ha "exit" → ciklus vége
        */
        int running = 1;

        while (running)
        {
            printf("Parent: reading to buffer...\n");

            char buffer[MEMSIZE];
            if (fgets(buffer, MEMSIZE, stdin) == NULL)
            {
                /*
                    fgets() NULL-t ad vissza EOF-nál (pl. Ctrl+D) vagy hiba esetén.
                    Ilyenkor kilépünk, mintha "exit"-et kapnánk.
                */
                printf("Parent: EOF vagy olvasasi hiba, kilépés...\n");
                szemafor_muvelet(semid, -1);
                strcpy(s->text, "exit");
                szemafor_muvelet(semid, 1);
                break;
            }

            /*
                Az fgets() megtartja a '\n' karaktert a sor végén —
                le kell vágnunk, különben a strcmp("exit") soha nem egyezik.
            */
            buffer[strlen(buffer) - 1] = '\0';

            printf("Parent: read to buffer: %s\n", buffer);

            /*
                Kritikus szakasz — szemafor védi az osztott memória írását.
                down(-1): belépés, érték 1→0
            */
            szemafor_muvelet(semid, -1); // Down

            strcpy(s->text, buffer);

            /*
                Kétirányú kommunikáció: a gyermek az előző körben
                kiszámolta és visszaírta a textSize-t.
                A szülő most kiolvassa — szemaforon belül, biztonságosan.
            */
            printf("Parent: previous length: %i\n", s->textSize);

            // up(+1): kilépés a kritikus szakaszból, érték 0→1
            szemafor_muvelet(semid, 1); // Up

            if (strcmp(buffer, "exit") == 0)
                running = 0;
        }

        /*
            shmdt() — szülő lecsatolja az osztott memóriát.
            waitpid() a gyermek biztos befejezéséig vár.
        */
        if (shmdt(s) == -1)
            perror("shmdt (szulo)");

        int status;
        pid_t finished;
        do
        {
            finished = waitpid(child, &status, 0);
        }
        while (finished == -1 && errno == EINTR);

        if (finished == -1)
            perror("waitpid");
        else if (WIFEXITED(status))
            printf("Gyerek (PID %d) kilépett, státusz: %d\n",
                   finished, WEXITSTATUS(status));

        szemafor_torles(semid);

        if (shmctl(sh_mem_id, IPC_RMID, NULL) == -1)
            perror("shmctl (IPC_RMID)");
    }
    else if (child == 0)
    {
        /*
            ---- GYERMEK FOLYAMAT — osztott memóriából olvas, textSize-t ír vissza ----

            Ciklus:
            1. Vár 2 másodpercet (a szülőnek időt ad az írásra)
            2. Szemafor down → kritikus szakasz
            3. Kiolvassa a text mezőt
            4. Visszaírja a hosszát a textSize mezőbe (kétirányú kommunikáció)
            5. Szemafor up → kritikus szakasz vége
            6. Ha "exit" → ciklus vége
        */
        int running = 1;

        while (running)
        {
            sleep(2); // a szülőnek idő kell a beolvasáshoz
            printf("Child: Waiting...\n");

            /*
                Kritikus szakasz — szemafor védi az osztott memória olvasását.
                Ha a szülő épp ír (bent van a kritikus szakaszban),
                a gyermek itt blokkolódik, amíg a szülő up()-ot nem hív.
            */
            szemafor_muvelet(semid, -1); // Down

            printf("Child: Reading: %s\n", s->text);

            if (strcmp(s->text, "exit") == 0)
                running = 0;

            /*
                Kétirányú kommunikáció: a gyermek kiszámolja és visszaírja
                a szöveg hosszát, amit a szülő a következő körben kiolvas.
            */
            s->textSize = strlen(s->text);

            szemafor_muvelet(semid, 1); // Up
        }

        if (shmdt(s) == -1)
        {
            perror("shmdt (gyerek)");
            exit(EXIT_FAILURE);
        }

        exit(EXIT_SUCCESS);
    }

    return EXIT_SUCCESS;
}

/*
    ÖSSZEFOGLALÁS — kétirányú kommunikáció osztott memóriával és mutexszel:

    MUTEX (kölcsönös kizárás) szemaforral:
    - Kezdőérték = 1 → bármelyik fél először beléphet
    - down(-1) → belépés a kritikus szakaszba (érték: 1→0)
    - up(+1)   → kilépés a kritikus szakaszból (érték: 0→1)
    - Egyszerre csak EGY folyamat lehet bent → nincs race condition

    KÉTIRÁNYÚ KOMMUNIKÁCIÓ:
    - Szülő → Gyermek: s->text (szülő ír, gyermek olvas)
    - Gyermek → Szülő: s->textSize (gyermek ír, szülő olvassa vissza)
    - Mindkét irány ugyanazon az osztott memória struktúrán keresztül megy

    ÖSSZEHASONLÍTÁS az előző példával:

    Előző példa (szinkronizáció):          Ez a példa (mutex):
    - Kezdőérték = 0 (zárt)               - Kezdőérték = 1 (nyitott)
    - Egyirányú, egyszeri                 - Kétirányú, ismétlődő ciklus
    - Szülő ír, gyermek vár              - Bármelyik léphet be először
    - fork() előtti írás is elég         - Dinamikus, futás közbeni sync
*/