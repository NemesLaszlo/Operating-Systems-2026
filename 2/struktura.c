#include <stdio.h>
#include <stdlib.h>
#include <string.h>


struct driver
{
    unsigned int id;
    char name[50];
    char email[50];
    char phone[50];
    unsigned int exp;
};


/*
    GLOBAL VARIABLE: id

    Purpose:
    Stores the next available unique identifier for new drivers.

    Logic:
    - Program reads the file during startup
    - Finds the highest existing ID
    - Increments it for next insert
*/
unsigned int id = 1;



/*
    FUNCTION: read_in_console_messages

    PURPOSE:
    Safely reads a full line of text input from the console.

    PARAMETERS:
    console_text -> Message displayed to user
    result       -> Buffer where input is stored

    IMPORTANT DETAILS:
    - Uses scanf with a length limiter (%49) to prevent buffer overflow
    - Leading space in format string skips leftover newline characters
    - Allows spaces inside input (unlike %s)
*/
void read_in_console_messages(const char *console_text, char *result)
{
    printf("%s", console_text);

    // Read up to 49 characters until newline
    scanf(" %49[^\n]", result);
}



/*
    FUNCTION: file_creation

    PURPOSE:
    Ensures that the database file exists.

    HOW IT WORKS:
    - Opens file in append mode ("a")
    - If file exists → nothing happens
    - If file does not exist → it is created

    WHY APPEND MODE:
    Prevents accidental overwriting of existing database.
*/
void file_creation()
{
    FILE *file = fopen("adatbazis.txt", "a");

    // Always check if file opening succeeded
    if (file == NULL)
    {
        fprintf(stderr, "File creation error!\n");
        exit(1);
    }

    fclose(file);

    // pointer reset
    file = NULL;
}



/*
    FUNCTION: file_read

    PURPOSE:
    Reads exactly one driver record from file.

    MEMORY MANAGEMENT:
    - Dynamically allocates memory for driver structure
    - Caller MUST free returned pointer

    RETURN VALUES:
    - Valid pointer if record was read successfully
    - NULL if end of file or reading error occurred
*/
struct driver *file_read(FILE *file)
{
    // Allocate memory for one driver record
    struct driver *actual = malloc(sizeof(struct driver));

    // Check memory allocation success
    if (actual == NULL)
        return NULL;

    /*
        fscanf reads formatted input from file.

        Expected format inside file:
        ID<TAB>Name<TAB>Email<TAB>Phone<TAB>Experience

        The return value must be 5,
        meaning all fields were successfully read.
    */
    if (fscanf(file, "%u\t%49[^\t]\t%49s\t%49s\t%u",
               &(actual->id),
               actual->name,
               actual->email,
               actual->phone,
               &(actual->exp)) != 5)
    {
        // If read failed → free memory and signal failure
        free(actual);
        actual = NULL;

        return NULL;
    }

    return actual;
}



/*
    FUNCTION: id_setup

    PURPOSE:
    Determines the next available driver ID.

    METHOD:
    - Opens database file
    - Reads all records sequentially
    - Stores the last read ID
    - Increments ID to ensure uniqueness
*/
void id_setup()
{
    FILE *infile = fopen("adatbazis.txt", "r");

    // Pointer used to store dynamically read driver records
    struct driver *actual = NULL;

    id = 0;

    if (infile != NULL)
    {
        /*
            Continue while reading returns valid data
        */
        while ((actual = file_read(infile)) != NULL)
        {
            // Store last ID
            id = actual->id;

            // Free dynamically allocated memory
            free(actual);
            // Defensive reset prevents dangling pointer
            actual = NULL;
        }

        fclose(infile);
        infile = NULL;
    }

    // Prepare next available ID
    id++;
}



/*
    FUNCTION: driver_creation

    PURPOSE:
    Creates and stores a new driver record.

    OPTIONAL FEATURE:
    - Supports cloning existing driver data
    - User may type "-" to keep original value
*/
void driver_creation(const struct driver *createdriver)
{
    /*
        Initialize structure to zero.
        Prevents garbage data if some fields are skipped.
    */
    struct driver a = {0};

    char input[50];
    unsigned int exp;

    // If cloning is requested → copy original driver
    if (createdriver != NULL)
        a = *createdriver;

    // Assign unique ID
    a.id = id++;

    /* ---------- NAME INPUT ---------- */
    read_in_console_messages("Name:\n", input);

    /*
        If user enters "-" and cloning is active,
        keep original value.
    */
    if (createdriver != NULL && strcmp(input, "-") == 0)
        strcpy(a.name, createdriver->name);
    else
        strcpy(a.name, input);

    /* ---------- EMAIL INPUT ---------- */
    read_in_console_messages("Email:\n", input);

    if (createdriver != NULL && strcmp(input, "-") == 0)
        strcpy(a.email, createdriver->email);
    else
        strcpy(a.email, input);

    /* ---------- PHONE INPUT ---------- */
    read_in_console_messages("Telefonszam:\n", input);

    if (createdriver != NULL && strcmp(input, "-") == 0)
        strcpy(a.phone, createdriver->phone);
    else
        strcpy(a.phone, input);

    /* ---------- EXPERIENCE INPUT ---------- */
    printf("Tapasztalat:\n");
    scanf("%u", &exp);

    if (createdriver != NULL && exp == 0)
        a.exp = createdriver->exp;
    else
        a.exp = exp;


    /*
        Open database file for appending new record.
    */
    FILE *outfile = fopen("adatbazis.txt", "a");

    if (outfile == NULL)
    {
        fprintf(stderr, "File open error!\n");
        exit(1);
    }

    /*
        Write driver record into file.
        fprintf returns number of written characters.
    */
    if (fprintf(outfile, "%u\t%s\t%s\t%s\t%u\n",
                a.id, a.name, a.email, a.phone, a.exp) > 0)
    {
        printf("Driver successfully added!\n");
    }
    else
    {
        printf("Write error occurred!\n");
    }

    fclose(outfile);
    outfile = NULL;
}



/*
    FUNCTION: main

    PURPOSE:
    Program entry point.
    Handles user interaction through menu system.

    PROGRAM FLOW:
    1. Ensure database file exists
    2. Initialize ID counter
    3. Display menu repeatedly
    4. Process user commands
*/
int main()
{
    int command;

    // Ensure database file exists
    file_creation();

    // Setup ID counter
    id_setup();

    while (1)
    {
        printf("\n-----MENU-----\n");
        printf("Driver_felvetel - 1\n");
        printf("Kilep - 2\n\n");

        /*
            getchar reads one character from user input.
            Used instead of scanf for simpler menu handling.
        */
        command = getchar();

        // Skip leftover newline characters
        if (command == '\n')
            continue;

        switch (command)
        {
            case '1':
                driver_creation(NULL);
                break;

            case '2':
                printf("Kilepes\n");
                exit(0);

            default:
                printf("Invalid command!\n");
        }

        /*
            Flush remaining characters in input buffer.
            Prevents accidental repeated command execution.
        */
        while (getchar() != '\n');
    }

    return 0;
}
