# Operating Systems
This repository contains the tasks / files and details of the ELTE Operating systems course.

### Contact
- Email: `laszlo.nemes@inf.elte.hu`
- MS Teams
- [Calendly](https://calendly.com/laszlo-nemes-elte) to schedule calls with me.

### ELTE server
- Server: `opsys.inf.elte.hu`
- Login: The `neptun code` in lowercase and then the `password` (infes azonosító)

### Tasks to be completed to successfully complete the class

2 "Take Home" assignments that build on each other, the first assignment is supplemented and must be continued in the second assignment.

2 ZH papers during the semester, one `theoretical` and one `practical`, both at the grade level (évfolyam szint).
- The dates of both will be available on ELTE Canvas (Lecture), towards the end of the semester.
- The `theoretical` paper takes place in the lecture hall (előadó).
- The `practical` paper can take place in the "Lovarda" room and the lecture hall, possibly in the Database lab as well at the same time due to staffing reasons.

##### Ratings

- The theoretical paper (quiz) consists of 15 questions, which you have 15 minutes to answer. The ZH is successful with 8 correct answers. The quiz is completed and submitted on Canvas.
  - 0-7 points one
  - 8-9 points two
  - 10-11 points three
  - 12-13 points four
  - 14-15 points excellent

 - The practical paper is 90 minutes long, solving a programming problem and submitting it in Canvas. Basic task mark two, then each correctly and COMPLETELY completed next task block plus one mark.


### Content and thematic:
- Basics of C, file management, binary, line-by-line file reading, pointers, "strings" etc.
- Forks and processes with C
- Handling of signals
- Use of pipes between processes (pipes, named pipes)
- Use of message queues between processes
- Shared memory, Semaphores
- Tasks summarizing all previously mentioned topics, "where a given situation must be simulated”.

### Compilement: 

```gcc``` and the name of the .c file. -> default output ```a.out```
We can run it as ./a.out

Different switches:

• -o switch to specify an output file option instead of the default a.out

e.g. ```gcc arg.c -o first``` and then we can run it as ```./first```

• -Werror switch, for displaying different errors

e.g. ```gcc -Werror arg.c -o first```

• Wall switch, which checks both errors and warnings

e.g. ```gcc -Wall arc.c -o first```
