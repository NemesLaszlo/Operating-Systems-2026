#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h> // stat struktúra és stat() függvény
#include <unistd.h>   
#include <time.h> // time_t és ctime()

int main(int argc,char** argv) {
 /*
    stat struktúra
    A fájl metaadatait tárolja:
    - méret
    - jogosultság
    - időbélyegek stb.
*/
 struct stat st;

/*
    stat()

    stat(filename, struct stat*)

    Lekéri a fájl metaadatait és betölti a struktúrába.
*/
 stat(argv[0],&st);  //fstat(file descriptor, stat structure)
 
/*
    st_mtime
    → utolsó módosítás ideje
    típusa: time_t
*/
 time_t t=st.st_mtime;
 printf("The last modification was: %s\n",ctime(&t)); //convert time_t to string 
 return 0;
}