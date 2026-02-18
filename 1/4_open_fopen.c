//Read and print out the file given in the parameter
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>     //open,creat
#include <sys/types.h> //open
#include <sys/stat.h>
#include <errno.h> //perror, errno
#include <string.h>
#include <unistd.h> // access

// fopen egy library call, open egy syscall, fopen egy magasabb szintű absztrakció, és az fopen használl buffered i/o amiért
// az gyorsabb mint az open, fopen is az opent hívja meg egyébként.
// pipeok esetén pl. a buffering nem kifizetődő, hiszen rögtön küldenénk, így hejzet függő melyiket használjuk.

void use_open_bin(char *fname)
{
  printf("\n**********\nUsing open - binary \n****************\n");
  int f = open(fname, O_RDONLY); // open esetén mode string mellett mode flageket is meg lehet adni mint itt.
  if (f < 0)
  {
    perror("Error at opening the file\n");
    exit(1);
  }

  char c; //instead of char, you can use any other type, struct as well
  while (read(f, &c, sizeof(c)))
  {                  //use write for writing
    printf("%c", c); //we prints out the content of the file on the screen
  }
  printf("\n");
  lseek(f, 0, SEEK_CUR);
  /*
  SEEK_SET	Offset is to be measured in absolute terms. (A fájl elejéhez képest számol.)
  SEEK_CUR	Offset is to be measured relative to the current location of the pointer.
  SEEK_END	Offset is to be measured relative to the end of the file.
  */
  /*
  lseek = LOW-LEVEL file positioning (system call)

  - Works with file descriptors (int f) returned by open()
  - Does NOT use buffering → directly moves the kernel file pointer
  - Mainly used together with open(), read(), write()

  NOTE:
  lseek should NOT be mixed with fopen/fread/fwrite because
  those use buffered I/O which can become inconsistent if the
  file pointer is moved outside the stdio buffering system.
*/
  read(f, &c, sizeof(c));
  printf("Current character - after positioning %c\n", c);
  close(f);
}

void use_fopen_text(char *fname)
{
  printf("\n******************\nUsing fopen -  reads in lines\n*****************\n");
  FILE *f;
  f = fopen(fname, "r");
  if (f == NULL)
  {
    perror("File opening error\n");
    exit(1);
  }
  char line[160];
  while (!feof(f)) // fájl vége vizsgálat
  {
    fgets(line, sizeof(line), f); // sor beolvasása megáll sor vége karakternél vagy fájl végénél
    printf("%s", line);
  }
  printf("\n");
  fclose(f);
}

void use_fopen_bin(char *fname)
{
  printf("\n******************\nUsing fopen -  binary \n*****************\n");
  FILE *f;
  f = fopen(fname, "rb");
  if (f == NULL)
  {
    perror("File opening error\n");
    exit(1);
  }
  char c; //instead of char, you can use any other type, struct as well
  while (!feof(f)) // (fread(&c, sizeof(c), 1, f) == 1) -> Ez jobb lenne, az fread ad visszatérési értéket
  {
    fread(&c, sizeof(c), sizeof(c), f); //use fwrite for writing
    /*
    fread = bináris adat beolvasása fájlból
    size_t fread(void *ptr, size_t size, size_t count, FILE *stream);

    - ptr
    Ide kerül az adat.
    void* → bármilyen típusú adat mutatója lehet.

    - size
    Egyetlen elem mérete bájtban.
    sizeof(c) → itt 1 bájt, mert char.

    - count
    Hány darab ilyen méretű elemet olvassunk.
    sizeof(c) → itt 1 elem (tehát 1 karakter)

    - stream
    A fájl pointer (FILE*)
    */
    printf("%c", c);
  }
  printf("\n");
  fseek(f, 0, SEEK_SET); // fájl elejére ugrás
  /*
  fseek = HIGH-LEVEL file positioning (C standard library)

  - Works with FILE* streams returned by fopen()
  - Uses buffered I/O → stdio keeps an internal buffer for performance
  - Keeps the buffer and file pointer synchronized
  - Should be used with fread(), fwrite(), fgets(), etc.

  Compared to lseek:
    fseek handles buffering safely
    lseek works directly on the OS file descriptor

  Rule of thumb:
    open()  → use lseek()
    fopen() → use fseek()
*/
  fread(&c, sizeof(c), sizeof(c), f);
  printf("First character - after positioning %c\n", c);

  fclose(f);
}

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    perror("Give a filename as a command line argument\n");
    exit(1);
  }
  if (access(argv[1], F_OK) != 0)
  {
    perror("The file is not exist!\n");
    exit(1);
  }
  use_fopen_text(argv[1]);
  use_open_bin(argv[1]);
  use_fopen_bin(argv[1]);
  return 0;
}