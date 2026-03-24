#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/*
    Globális mutató az osztott memóriára.
    Azért globális, hogy a child1() függvény is elérhesse
    anélkül, hogy paraméterként kellene átadni.
    Fontos: shmdt() után ez a mutató ÉRVÉNYTELEN lesz — ne használd utána
*/
char *s;

/*
    A főfolyamat PID-je.
    Jelenleg nincs közvetlen felhasználása ebben a példában,
    de jelzésalapú szinkronizációnál (pl. SIGUSR1) szükség lenne rá.
    Ld. sigaction referencia példa.
*/
pid_t mainszalertek = 0;

/*
    child1() — gyermek folyamat létrehozása és futtatása

    A fork() két különböző kontextusban tér vissza:
    - Szülőben: a gyermek PID-jével (> 0)  → visszaadjuk a hívónak
    - Gyermekben: 0-val                    → elvégzi a munkát, majd kilép
    - Hiba esetén: -1                      → kilépünk

    A gyermek az osztott memóriából olvas, majd lecsatlakozik (shmdt),
    és EXIT_SUCCESS-szel kilép.
*/
pid_t child1()
{
    pid_t szal = fork();

    if (szal == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (szal > 0)
    {
        // Szülő: visszaadjuk a gyermek PID-jét a main()-nek
        return szal;
    }

    /*
        ---- GYERMEK FOLYAMAT ----

        Idáig csak akkor jutunk, ha fork() 0-t adott vissza.
        A gyermek örökli a szülő shmat() leképezését,
        tehát az 's' mutató itt is érvényes és ugyanarra a
        fizikai memóriaterületre mutat.
    */

    /*
        A szülő a fork() ELŐTT írt az osztott memóriába (strcpy),
        ezért a gyermek garantáltan a kész adatot olvassa ki —
        nincs race condition.

        printf() helyett write() async-signal-safe lenne,
        de itt nem jelzéskezelőből hívjuk, így printf() elfogadható.
    */
    printf("A gyerek ezt olvasta az osztott memoriabol: %s", s);
    fflush(stdout); // pufferelt kimenet kiürítése fork() után kötelező

    /*
        shmdt() — a gyermek lecsatlakozik az osztott memóriától.
        Csak a gyermek saját virtuális leképezését szünteti meg.
        A memóriazóna MEGMARAD — a szülő és az IPC_RMID még kezeli.
    */
    if (shmdt(s) == -1)
    {
        perror("shmdt (child)");
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
    // A gyermek soha nem tér vissza a child1()-ből — exit() lezárja
}

int main(int argc, char *argv[])
{
    int status;
    pid_t child_pid;
    key_t kulcs;
    int oszt_mem_id;

    /*
        ftok() — IPC kulcs generálás
        Az argv[0] fájl inode-ja és az '1' projekt-azonosító alapján
        egyedi kulcsot állít elő. Azonos argv[0] + azonosító → azonos kulcs,
        így több folyamat megtalálja egymás memóriazónáját.
    */
    if (argc < 1 || argv[0] == NULL)
    {
        fprintf(stderr, "argv[0] nem elérhető\n");
        return EXIT_FAILURE;
    }

    // a parancs nevevel es az 1 verzio szammal kulcs generalas
    kulcs = ftok(argv[0], 1);
    if (kulcs == -1)
    {
        perror("ftok");
        return EXIT_FAILURE;
    }

    /*
        shmget() — osztott memóriazóna létrehozása vagy megnyitása.
        - IPC_CREAT: hozza létre, ha még nem létezik
        - S_IRUSR | S_IWUSR: olvasás + írás csak a tulajdonosnak
    */
    oszt_mem_id = shmget(kulcs, 500, IPC_CREAT | S_IRUSR | S_IWUSR);
    if (oszt_mem_id == -1)
    {
        perror("shmget");
        return EXIT_FAILURE;
    }

    /*
        shmat() — csatolás a folyamat virtuális címteréhez.
        - NULL: a kernel választja meg a csatolási címet (ajánlott)
        - 0: írás-olvasás mód

        Visszatérési értéke hiba esetén (void *)-1, NEM NULL!
        Ez egy gyakori csapda: NULL-ra ellenőrizni helytelen.
    */
    s = shmat(oszt_mem_id, NULL, 0);
    if (s == (void *)-1)
    {
        perror("shmat");
        shmctl(oszt_mem_id, IPC_RMID, NULL); // takarítás hiba előtt
        return EXIT_FAILURE;
    }

    // A főfolyamat PID-jének eltárolása (jelzésalapú szinkronhoz hasznos)
    mainszalertek = getpid();

    char buffer[] = "Valami iras! \n";
    // beirunk a memoriaba (FORK ELŐTT VAN)
    strcpy(s, buffer);

    /*
        Child létrehozása CSAK EZUTÁN.
        child1() a fork() szülő ágán visszaadja a gyermek PID-jét.
    */
    child_pid = child1();

    /*
        ---- SZÜLŐ FOLYAMAT ----

        Idáig csak a szülő folyamat jut el:
        a child1() a gyermek ágon exit()-tel lép ki,
        a szülő ágon visszatér a gyermek PID-jével.
    */

    /*
        shmdt() — szülő lecsatlakozik az osztott memóriától.
        Az 's' mutató ettől fogva ÉRVÉNYTELEN a szülőben.
        A memóriazóna még létezik — a gyermek még olvashatja.
    */
    if (shmdt(s) == -1)
    {
        perror("shmdt (parent)");
        shmctl(oszt_mem_id, IPC_RMID, NULL); // takarítás hiba előtt
        return EXIT_FAILURE;
    }

    /*
        waitpid() — megvárjuk a gyermek kilépését.

        EINTR kezelés: ha egy jelzés megszakítja a várakozást,
        a waitpid() -1-et ad vissza és errno == EINTR lesz.
        Ilyenkor újra kell próbálni — ld. sigaction referencia példa.

        waitpid() opciók (harmadik paraméter):
        - 0          → alapértelmezett: megvárja a kilépést
        - WNOHANG    → azonnal visszatér, ha a gyermek még fut
        - WUNTRACED  → visszatér, ha a gyermek megáll (SIGSTOP)
        - WCONTINUED → visszatér, ha a gyermek folytatódik (SIGCONT)
    */
    pid_t finished;

    do
    {
        finished = waitpid(child_pid, &status, 0);
    }
    while (finished == -1 && errno == EINTR);

    if (finished == -1)
    {
        perror("waitpid");
        shmctl(oszt_mem_id, IPC_RMID, NULL); // takarítás hiba előtt
        return EXIT_FAILURE;
    }

    // Gyermek kilépési kódjának kiírása (opcionális, de hasznos debugging-hoz)
    if (WIFEXITED(status))
    {
        printf("Gyerek (PID %d) kilépett, státusz: %d\n",
               finished, WEXITSTATUS(status));
    }

    /*
        shmctl(IPC_RMID) — osztott memóriazóna törlésének jelölése.
        A kernel ténylegesen akkor szabadítja fel, ha az utolsó
        csatlakozó folyamat is lecsatlakozott (shmdt).
        Mivel mindkét folyamat már shmdt()-t hívott, a törlés azonnali.

        shmctl() egyéb lehetséges parancsok:
        - IPC_STAT  → memória metaadatainak lekérdezése (struct shmid_ds*)
        - IPC_SET   → memória tulajdonságainak beállítása (struct shmid_ds*)
    */

    /*
        IPC_RMID - torolni akarjuk a memoriat,
        ekkor nem kell 3. parameter
    */
    if (shmctl(oszt_mem_id, IPC_RMID, NULL) == -1)
    {
        perror("shmctl");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
    ÖSSZEFOGLALÁS — az osztott memória életciklusa ebben a példában:

    [main]
      1. ftok()              → egyedi IPC kulcs generálás
      2. shmget()            → memóriazóna létrehozása
      3. shmat()             → csatolás (s mutató érvényes lesz)
      4. strcpy(s, buffer)   → írás az osztott memóriába
      5. fork()  [child1()]  → gyermek örökli az 's' leképezést

    [child]
      6. printf(s)           → olvasás az osztott memóriából
      7. shmdt(s)            → lecsatolás
      8. exit()

    [parent]
      9. shmdt(s)            → lecsatolás (s érvénytelen lesz)
     10. waitpid()           → megvárjuk a gyermeket
     11. shmctl(IPC_RMID)    → memóriazóna törlése
*/