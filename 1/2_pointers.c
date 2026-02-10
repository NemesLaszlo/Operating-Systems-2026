#include <stdio.h>  // Be és kimenetekhez határoz meg változókat,makrókat és funckiókat.
#include <stdlib.h> // Általános függvény végrehajtásokhoz.

//argc = hány db karakter
//argv = argumentumok listája
int main(int argc, char **argv)
//char** means an array of character arrays = array of strings
{
    int n;
    n = 25;

    // & operátor → visszaadja egy változó memóriacímét
    int *k = &n; // Pointer ami n címére mutat

    printf("n erteke = %i\n", n);
    printf("n cime = %p\n", &n);

    printf("k altal tarolt cim = %p\n", k);
    printf("k altal mutatott ertek = %i\n", *k);

    return 0;
}