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
    szemafor_letrehozas() — szemafor létrehozása és inicializálása

    A szemafor egy egész szám, amelyet a kernel kezel.
    Értéke jelzi, hogy a védett erőforrás szabad-e:
    - 0: foglalt (a down() műveletet hívó folyamat blokkolódik)
    - 1: szabad (a down() művelet azonnal továbbenged)

    Paraméterei:
    - pathname: az ftok()-hoz használt fájl elérési útja
    - szemafor_ertek: a szemafor kezdőértéke (0 = zárt, 1 = nyitott)

    Visszatérési értéke: a szemafor azonosítója (semid)
*/
int szemafor_letrehozas(const char *pathname, int szemafor_ertek)
{
    int semid;
    key_t kulcs;

    /*
        ftok() — IPC kulcs generálás a szemaforhoz.
        Ugyanaz a mechanizmus, mint az osztott memóriánál,
        de '2' projekt-azonosítóval, hogy ne ütközzön a memória kulcsával.
    */
    kulcs = ftok(pathname, 2);
    if (kulcs == -1)
    {
        perror("ftok (szemafor)");
        exit(EXIT_FAILURE);
    }

    /*
        semget() — szemafor készlet létrehozása vagy megnyitása.
        - kulcs: egyedi IPC kulcs
        - 1: a készletben lévő szemaforok száma (nekünk egy elég)
        - IPC_CREAT | S_IRUSR | S_IWUSR: létrehozás, csak tulajdonosnak
    */
    semid = semget(kulcs, 1, IPC_CREAT | S_IRUSR | S_IWUSR);
    // semget 2. parameter is the number of semaphores
    if (semid == -1)
    {
        perror("semget");
        exit(EXIT_FAILURE);
    }

    /*
        semctl(SETVAL) — a szemafor kezdőértékének beállítása.
        - semid: a szemafor készlet azonosítója
        - 0: a készleten belüli szemafor indexe (csak egy van)
        - SETVAL: beállítja a szemafor értékét
        - szemafor_ertek: a kívánt kezdőérték

        Ha 0-ra inicializálunk → a szemafor "le van zárva":
        az első down() (-1) műveletet hívó folyamat blokkolódik,
        amíg a másik folyamat up() (+1)-et nem hív.
    */
    if (semctl(semid, 0, SETVAL, szemafor_ertek) == -1)
    {
        perror("semctl (SETVAL)");
        exit(EXIT_FAILURE);
    }

    return semid;
}

/*
    szemafor_muvelet() — szemafor le- vagy felnyitása (P/V művelet)

    A sembuf struktúra mezői:
    - sem_num: a szemafor indexe a készleten belül (0, ha csak egy van)
    - sem_op:  a művelet értéke
        +1 → up / signal / V: felszabadítja az erőforrást, ébreszt egy várakozót
        -1 → down / wait / P: lefoglalja az erőforrást, vagy blokkolódik, ha 0
         0 → vár, amíg a szemafor értéke 0 nem lesz (Tehát fordítva működik mint a down(): nem akkor enged tovább, ha szabad, hanem akkor, ha mindenki végzett (az érték visszaesett nullára))
    - sem_flg: módosító flag-ek
        SEM_UNDO  → kilépéskor automatikusan visszavon (biztonságos crash esetén)
        IPC_NOWAIT → nem blokkolódik, azonnal EAGAIN hibával tér vissza

    semop() — atomikusan hajtja végre a műveletet:
    harmadik paramétere a műveletek száma (most 1).
    -> A harmadik paraméter azt mondja meg, hogy hány sembuf műveletet hajtson végre egyszerre, nem a szemafor értékét.
*/
void szemafor_muvelet(int semid, int op)
{
    struct sembuf muvelet;

    muvelet.sem_num = 0; // This member specifies the index of the semaphore within the semaphore set, now we only have one, so it is a "hardcoded" value
    muvelet.sem_op = op; // op=1 up, op=-1 down
    muvelet.sem_flg = 0; // SEM_UNDO, IPC_NOWAIT

    if (semop(semid, &muvelet, 1) == -1) // 1 number of sem. operations
    {
        perror("semop");
        exit(EXIT_FAILURE);
    }
}

/*
    szemafor_torles() — szemafor készlet törlése

    semctl(IPC_RMID) azonnal eltávolítja a szemafor készletet a rendszerből.
    Minden blokkolt folyamat EINTR hibával ébred fel.
    A törlést csak a létrehozó (szülő) hívja, miután a gyermek már befejezte.
*/
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
    char *s;

    if (argc < 1 || argv[0] == NULL)
    {
        fprintf(stderr, "argv[0] nem elérhető\n");
        return EXIT_FAILURE;
    }

    /*
        ftok() — IPC kulcs az osztott memóriához.
        '1' projekt-azonosítóval — különböző a szemaforétól ('2'),
        így a két IPC objektum kulcsa nem ütközik.
    */
    kulcs = ftok(argv[0], 1);
    if (kulcs == -1)
    {
        perror("ftok (memoria)");
        return EXIT_FAILURE;
    }

    /*
        shmget() + shmat() — osztott memória létrehozása és csatolása.
        Ld. részletes magyarázat az osztott memória példában.
    */
    sh_mem_id = shmget(kulcs, MEMSIZE, IPC_CREAT | S_IRUSR | S_IWUSR);
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
        Szemafor létrehozása 0 kezdőértékkel → "le van zárva".

        FONTOS: a szemafort a fork() ELŐTT kell létrehozni,
        hogy mind a szülő, mind a gyermek ugyanazt a semid-et örökölje.

        Szinkronizáció menete:
        1. Szülő ír az osztott memóriába
        2. Szülő up(+1) → szemafor értéke 0→1 (felnyit)
        3. Gyermek down(-1) → szemafor értéke 1→0 (beenged, vagy vár ha még 0)
        4. Gyermek olvas az osztott memóriából
        5. Gyermek up(+1) → szemafor értéke 0→1 (felszabadít)
    */
    semid = szemafor_letrehozas(argv[0], 0);

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
            ---- SZÜLŐ FOLYAMAT ----

            A szülő először ír az osztott memóriába,
            MAJD szemafor up()-pal jelzi a gyermeknek, hogy olvashat.
            Ez garantálja, hogy a gyermek csak kész adatot lát.
        */
        char buffer[] = "I like something, test sentence!\n";
        printf("Szulo indul, kozos memoriaba irja: %s\n", buffer);
        strcpy(s, buffer);

        /*
            Szemafor up (+1): értéke 0→1.
            Ha a gyermek már blokkolt a down()-nál, most felébred.
            Ha még nem érte el a down()-t, az majd azonnal továbbenged.
        */
        printf("Szulo, szemafor up!\n");
        szemafor_muvelet(semid, 1); // Up

        /*
            shmdt() — szülő lecsatolja az osztott memóriát.
            Az 's' mutató ettől fogva érvénytelen a szülőben.
            A gyermek még csatolva van, ő még olvashatja.
        */
        if (shmdt(s) == -1)
        {
            perror("shmdt (szulo)");
        }

        /*
            waitpid() a gyermek biztos befejezéséig vár.
        */
        int status;
        pid_t finished;
        do
        {
            finished = waitpid(child, &status, 0);
        }
        while (finished == -1 && errno == EINTR);

        if (finished == -1)
        {
            perror("waitpid");
        }
        else if (WIFEXITED(status))
        {
            printf("Gyerek (PID %d) kilépett, státusz: %d\n",
                   finished, WEXITSTATUS(status));
        }

        /*
            Takarítás: szemafor törlése, majd osztott memória törlése.
            Sorrendje fontos: előbb a szemafort töröljük (a gyermek már nem
            használja), majd a memóriazónát.
        */
        szemafor_torles(semid);

        if (shmctl(sh_mem_id, IPC_RMID, NULL) == -1)
            perror("shmctl (IPC_RMID)");
    }
    else if (child == 0)
    {
        /*
            ---- GYERMEK FOLYAMAT ----

            Kritikus szakasz szemaforral védve:
            A gyermek megvárja, amíg a szülő feltölti az osztott memóriát,
            majd olvas belőle, végül jelzi, hogy végzett.
        */

        // critical section
        printf("Gyerek: Indula szemafor down!\n");

        /*
            Szemafor down (-1): ha értéke 0, blokkolódik.
            Felébred, amikor a szülő up(+1)-et hív.
            Értéke 1→0 lesz (belép a kritikus szakaszba).
        */
        szemafor_muvelet(semid, -1); // down, wait if necessary

        printf("Gyerek, down rendben, eredmeny: %s", s);

        /*
            Szemafor up (+1): értéke 0→1.
            Jelzi, hogy a gyermek végzett az olvasással —
            egy esetleges harmadik folyamat továbbengedhetné magát.
            Ebben a példában egy szülő + egy gyermek van, ezért
            ez a lépés nem feltétlenül szükséges, de jó szokás.
        */
        szemafor_muvelet(semid, 1); // up
        // end of critical section

        /*
            shmdt() — gyermek lecsatolja az osztott memóriát.
            A tényleges törlést a szülő végzi (IPC_RMID).
        */
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
    ÖSSZEFOGLALÁS — szemaforral védett osztott memória életciklusa:

    [main – fork() előtt]
      1. ftok('1')           → kulcs az osztott memóriához
      2. shmget()            → memóriazóna létrehozása
      3. shmat()             → csatolás (s mutató érvényes)
      4. ftok('2')           → kulcs a szemaforhoz (különböző!)
      5. semget()            → szemafor készlet létrehozása
      6. semctl(SETVAL, 0)   → szemafor értéke = 0 (zárt)
      7. fork()              → szülő + gyermek indul

    [szülő]
      8. strcpy(s, buffer)   → ír az osztott memóriába
      9. semop(+1)           → up: szemafor értéke 0→1, gyermek ébred
     10. shmdt(s)            → lecsatolás
     11. waitpid()           → megvárja a gyermeket
     12. semctl(IPC_RMID)    → szemafor törlése
     13. shmctl(IPC_RMID)    → memóriazóna törlése

    [gyermek]
      8. semop(-1)           → down: vár, amíg értéke > 0 (blokkolódik)
      9. printf(s)           → olvas az osztott memóriából
     10. semop(+1)           → up: szemafor értéke 0→1 (felszabadít)
     11. shmdt(s)            → lecsatolás
     12. exit()

    MIÉRT KELL A SZEMAFOR?
    Az osztott memória önmagában NEM szinkronizál.
    Ha fork() UTÁN írna a szülő, a gyermek esetleg hamarabb futna —
    és üres / részleges adatot olvasna ki.
    A szemafor garantálja a "szülő előbb ír, gyermek utána olvas" sorrendet.

    KÜLÖNBSÉG az előző (sleep-es / fork-előtti írás) megoldáshoz képest:
    - fork() előtti írás: egyszerűbb, de csak egyirányú, egyszeri kommunikációra jó
    - szemafor: dinamikus szinkronizáció, oda-vissza kommunikációra is alkalmas
*/