#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>

/*
ready változó:

A signal handler és a főprogram közösen használja.

Miért nem lehet sima int?

Mert:
- A handler aszinkron módon fut.
- A fordító optimalizálhatná a változót (regiszterben tarthatná).
- Részleges írás/olvasás történhetne.

sig_atomic_t garantálja:
- Az írás és olvasás atomi művelet.
- Nem szakítható meg közben.

volatile:
- Megakadályozza, hogy a fordító optimalizálja.
- Minden olvasás valóban memóriából történik.
*/
volatile sig_atomic_t ready = 0;
pid_t mainProcessValue = 0;

/*
Signal handler:

Amikor a folyamat megkapja a SIGUSR1 jelet,
a normál végrehajtás megszakad,
és a kernel meghívja ezt a függvényt.

Fontos szabály:
Signal handler-ben csak async-signal-safe
függvények hívhatók biztonságosan.

Ezért itt:
- NINCS printf()
- csak egy egyszerű változó növelés történik
*/
void readyHandler(int sig)
{
    if (sig == SIGUSR1)
    {
        ready++;
    }
}

pid_t child1()
{
    pid_t process = fork();

    if (process == -1)
        exit(-1);

    if (process > 0)
    {
        return process;   /* Szülő visszatér */
    }

    sleep(1);  
    kill(mainProcessValue, SIGUSR1);

    exit(0);
}

pid_t child2()
{
    pid_t process = fork();

    if (process == -1)
        exit(-1);

    if (process > 0)
    {
        return process;   /* Szülő visszatér */
    }

    sleep(2);
    kill(mainProcessValue, SIGUSR1);

    exit(0);
}

int main()
{
    mainProcessValue = getpid();

    /* ===== Jel blokkolása race condition ellen ===== */
    /*
    Miért blokkoljuk a SIGUSR1 jelet fork előtt?

    Probléma (race condition):

    Ha a gyermek nagyon gyors,
    elküldheti a jelet még azelőtt,
    hogy a szülő belépne a várakozó ciklusba.

    Ekkor a jel "elveszhet",
    és a szülő örökké várakozna.

    Megoldás:

    1. SIGUSR1 blokkolása (SIG_BLOCK)
    2. Gyermekek létrehozása
    3. sigsuspend() segítségével atomi várakozás

    Ez a POSIX ajánlott minta.
    */
    sigset_t block_mask, old_mask; // Jelmaszk struktúrák (blokkolt jelek halmaza)

    sigemptyset(&block_mask);  // Kiürítjük a maszkot (kezdetben nincs blokkolt jel)
    sigaddset(&block_mask, SIGUSR1); // Hozzáadjuk a SIGUSR1 jelet a blokkolandó jelekhez

    /*
    sigprocmask():

    Lehetővé teszi jelek blokkolását vagy feloldását.

    SIG_BLOCK:
        Hozzáadja a megadott jeleket a blokkolt maszkhoz.

    block_mask:
        Az újonnan blokkolandó jelek halmaza.

    old_mask:
        Elmenti a korábbi maszkot,
        amit később vissza tudunk állítani.

    Amíg a jel blokkolt,
    a kernel nem futtatja a handler-t,
    hanem "pending" állapotban tartja.
    */
    sigprocmask(SIG_BLOCK, &block_mask, &old_mask);
    // A SIGUSR1 blokkolása a folyamatban
    // A korábbi maszk elmentése old_mask-ba
    // Ettől kezdve ha SIGUSR1 érkezik, "pending" állapotba kerül
    // A handler NEM fut le azonnal

    /* ===== Handler beállítása sigaction-nel ===== */
    /*
    Miért sigaction és nem signal?

    A signal():
    - történelmileg eltérő implementációk
    - nem teljesen hordozható
    - bizonyos rendszereken automatikusan visszaáll default-ra

    A sigaction():
    - POSIX szabvány
    - determinisztikus viselkedés
    - finomhangolható (mask, flags)

    struct sigaction mezői:

    sa_handler:
        A meghívandó függvény címe.

    sa_mask:
        Azon jelek halmaza,
        amelyeket a handler futása alatt blokkolni kell.

    sa_flags:
        Viselkedési opciók (pl. SA_RESTART).
    */
    struct sigaction sa; // Struktúra a jelkezelés beállításához
    sa.sa_handler = readyHandler; // Megadjuk a meghívandó függvényt
    sigemptyset(&sa.sa_mask); // A handler futása alatt nem blokkolunk további jeleket
    sa.sa_flags = 0; // Nincs extra viselkedési opció (pl. SA_RESTART)

    // A kernel jelzi, hogy ha SIGUSR1 érkezik, akkor a readyHandler függvényt kell meghívni
    if (sigaction(SIGUSR1, &sa, NULL) == -1) // A NULL - oldact - Ez az a hely, ahova a kernel elmentené a korábbi beállítást. ÍGy ha null - "Nem érdekel a régi signal handler beállítás, nem akarom elmenteni".
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    /* ===== Gyermekek létrehozása ===== */
    pid_t child1_pid = child1();
    pid_t child2_pid = child2();

    /* ===== Várakozás busy waiting nélkül ===== */
    /*
    sigsuspend():

    Atomikus művelet:
    1. Ideiglenesen beállítja a megadott maszkot
    2. Felfüggeszti a folyamatot
    3. Vár egy jelre
    4. Handler lefut
    5. Visszatér

    Miért fontos az atomikusság?

    Ha külön:
        - feloldanánk a blokkolást
        - majd pause()-t hívnánk

    akkor a jel a kettő között érkezhetne,
    és elveszne.

    sigsuspend ezt a hibát akadályozza meg.
    */
    while (ready < 1)
        sigsuspend(&old_mask); 
        // Atomikusan:
        // 1. Ideiglenesen visszaállítja az eredeti maszkot (vagyis visszaállítja az eredeti állapotot, SIGUSR1 NINCS blokkolva)
        // 2. A folyamat alvó állapotba kerül
        // 3. Ha jel érkezik → handler lefut
        // 4. sigsuspend visszatér -1 értékkel (EINTR)

    puts("Child1 ready!");

    while (ready < 2)
        sigsuspend(&old_mask);

    puts("Child2 ready!");

    /* Maszk visszaállítása */
    sigprocmask(SIG_SETMASK, &old_mask, NULL);

    /* ===== Gyermekek bevárása ===== */
    int status;

    waitpid(child1_pid, &status, 0);

    if (WIFEXITED(status))
        printf("Child1 exited with code %d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("Child1 killed by signal %d\n", WTERMSIG(status));

    waitpid(child2_pid, &status, 0);

    if (WIFEXITED(status))
        printf("Child2 exited with code %d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("Child2 killed by signal %d\n", WTERMSIG(status));

    printf("Done\n");

    return 0;
}