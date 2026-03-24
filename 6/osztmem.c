#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

/*
Az osztott vagy közös memória segítségével megoldható, hogy két vagy több folyamat ugyanazt a memóriarészt használja. Az osztott memóriazónák általi kommunikáció elvei:

- Egy folyamat létrehoz egy közös memóriazónát. A folyamat azonosítója bekerül a memóriazónához rendelt struktúrába.

- A létrehozó folyamat hozzárendel az osztott memóriához egy numerikus kulcsot, amelyet minden ezt a memóriarészt használni kívánó folyamatnak ismernie kell. Ezt a memóriazónát az shmid változó azonosítja.

- A létrehozó folyamat leszögezi a többi folyamat hozzáférési jogait az illető zónához. Azért, hogy egy folyamat (beleértve a létrehozó folyamatot is) írni és olvasni tudjon a közös memóriarészből, hozzá kell rendelnie egy virtuális címterületet.

Ez a kommunikáció a leggyorsabb, hiszen az adatokat nem kell mozgatni a kliens és a szerver között.
*/

int main(int argc, char *argv[])
{
    if (argc < 1) {
        fprintf(stderr, "argv[0] nem elérhető\n");
        exit(EXIT_FAILURE);
    }

    pid_t pid;
    key_t kulcs;
    int oszt_mem_id;
    char *s;

    /*
    ftok() — kulcs generálás
    A 'projekt azonosító' (itt 1) és az argv[0] fájl inode-ja alapján
    egyedi IPC kulcsot generál. Ugyanazon kulcsot kapják azok a folyamatok,
    amelyek ugyanazzal az argv[0] fájllal és azonosítóval hívják meg az ftok()-t.
    */
    kulcs = ftok(argv[0], 1);
    if (kulcs == -1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }

    /*
    shmget() — osztott memória létrehozása vagy megnyitása
    - kulcs: az IPC kulcs, ami azonosítja a memóriazónát
    - 500: a memória mérete bájtban
    - IPC_CREAT: hozza létre, ha még nem létezik
    - S_IRUSR | S_IWUSR: olvasás + írás engedélyek a tulajdonos számára
    Visszatérési értéke az oszt_mem_id (shared memory identifier),
    amellyel a memóriazóna a továbbiakban azonosítható.
    */
    oszt_mem_id = shmget(kulcs, 500, IPC_CREAT | S_IRUSR | S_IWUSR);
    if (oszt_mem_id == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    /*
    shmat() — csatlakozás az osztott memóriához (attach)
    A folyamat saját virtuális címterébe képezi le az osztott memóriát.
    - oszt_mem_id: az shmget() által visszaadott azonosító
    - NULL: a kernel választja meg a csatolási címet
    - 0: írás-olvasás mód (SHM_RDONLY esetén csak olvasás)
    Visszaad egy char* mutatót, amely a közös memória kezdőcímére mutat.
    */
    s = shmat(oszt_mem_id, NULL, 0);
    if (s == (char *)-1) {
        perror("shmat");
        shmctl(oszt_mem_id, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid == -1) {
        perror("fork");
        shmdt(s);
        shmctl(oszt_mem_id, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    if (pid > 0)
    {
        /*
        SZÜLŐ FOLYAMAT:
        Beleír az osztott memóriába, majd elengedi a saját leképezését.
        Ezután megvárja, hogy a gyermek kiolvasson, majd törli a memóriazónát.
        */
        char buffer[] = "Valami iras!\n";
        // beirunk a memoriaba
        strcpy(s, buffer);

        /*
        shmdt() — lecsatlakozás az osztott memóriától (detach)
        A folyamat saját virtuális címteréből eltávolítja a leképezést,
        de az osztott memória maga MEGMARAD a rendszerben — más folyamatok
        továbbra is hozzáférhetnek. Az s mutató ezután ÉRVÉNYTELEN.
        */
        shmdt(s);
        //	s[0]='B';  //ez segmentation fault hibat ad

        // megvárjuk, hogy a gyerek kiolvassa
        int status;
        pid_t w;
        do {
            w = waitpid(pid, &status, 0);
        } while (w == -1 && errno == EINTR); // újrapróbálkozás, ha waitpid megszakadt jel miatt

        if (w == -1) {
            perror("waitpid");
        } else if (WIFEXITED(status)) {
            printf("Gyerek (PID %d) kilépett, státusz: %d\n", w, WEXITSTATUS(status));
        }

        /*
        shmctl() — osztott memória vezérlése
        - IPC_RMID: törli a memóriazónát (a rendszer eltávolítja, ha már senki sem csatlakozik hozzá)
        - IPC_STAT: osztott memória adatainak lekérdezése (harmadik param: struct shmid_ds*)
        - IPC_SET: beállítja a memória tulajdonságait (harmadik param: struct shmid_ds*)
        */
        // IPC_RMID- torolni akarjuk a memoriat, ekkor nem kell 3. parameter
        // IPC_STAT- osztott memoria adatlekerdezes a 3. parameterbe,
        shmctl(oszt_mem_id, IPC_RMID, NULL); // shared memory control
    }
    else if (pid == 0)
    {
        /*
        GYERMEK FOLYAMAT:
        Vár, majd kiolvas az osztott memóriából.
        A sleep(1) nem garantált szinkronizáció — valódi szinkronhoz
        szemafort vagy jelzést (pl. SIGUSR1) érdemes használni.
        */
        sleep(1);
        printf("A gyerek ezt olvasta az osztott memoriabol: %s", s);

        /*
        A gyerek is lecsatlakozik az osztott memóriától.
        shmdt() után az s mutató érvénytelen — a szülő IPC_RMID-je
        majd ténylegesen törli a memóriazónát.
        */
        shmdt(s);

        exit(EXIT_SUCCESS);
    }

    return 0;
}

/*
ÖSSZEFOGLALÁS — az osztott memória életciklusa:

1. shmget()  -> memóriazóna létrehozása / azonosítójának megszerzése
2. shmat()   -> csatolás a folyamat virtuális címteréhez (s mutatón elérhető)
3. [írás/olvasás az s mutatón keresztül]
4. shmdt()   -> lecsatolás (s mutató érvénytelen lesz, a memória megmarad)
5. shmctl(IPC_RMID) -> tényleges törlés (ha már senki sem csatlakozik)

FONTOS KÜLÖNBSÉG shmdt() és shmctl(IPC_RMID) között:
- shmdt():  csak az adott folyamat leképezését szünteti meg
- IPC_RMID: az egész memóriazónát jelöli törlésre (a kernel törli,
            ha az utolsó csatlakozó folyamat is lecsatlakozik)

MEGJEGYZÉS a sleep(1)-ről:
A gyermek sleep(1)-gyel "vár" a szülőre, de ez nem valódi szinkronizáció.
Ha a szülő lassabb lenne, a gyermek üres memóriát olvasna ki.
Valódi szinkronhoz érdemes szemafort (semget/semop) vagy SIGUSR1 jelzést
használni — a sigaction referencia példa.
*/