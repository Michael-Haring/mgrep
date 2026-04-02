# mgrep
  My multi-threaded version of the unix classic, grep. I was recently playing 
around with threads and made a thread pooling system, but struggled to find meaningful 
tasks to give to the threads to really put it to the test. A few days later I heard 
that grep was single threaded and I knew then I had to make my own multi-threaded version, 
and that searching a file for a pattern would make for a solid task.

## Overview
mgrep is roughly the same speed, could be slightly slower or slightly faster than grep 
depending on the options used, for small searches. As far as I know when the search 
gets large enough mgrep gets MUCH faster than grep. This is not as clear as I claim, and both 
grep and mgrep skip certain files. However if you take a look at the chart near the bottom you will see speed differences between grep and mgrep for various sized searches.

grep not searching every file became apparent when I was writing a bash script that would 
run them both and compare the results. More on file extensions that are skipped by default in 
the "Options" section.


## Quick Start
### Dependencies
- CMake
- C++ Compiler


```bash
git clone https://github.com/Michael-Haring/mgrep.git
cd mgrep
```
From the project root:
```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cd build-release
make
```
then to test your build:
```bash
./test
```
Finally you can run the almighty mgrep...
```bash
./mgrep
```
### Warning
If this text is here then you should be warned: 


The testing suite is basically empty
and the Catch2 testing suite is also non existant past the ThreadPool initial tests.
This program does work well, but I do not claim that it is as efficient or as robust as 
the famous grep.

## Features
### ThreadPool Creation
First the ThreadPool object is created, which also creates a number of threads that is 
equal to 75% of the return value from std::threads::hardware_concurrancy(). The quick version 
is that these threads are put to sleep until they recieve a signal to check if there is a file
to read.

## Options
The legendary getopt provides us with several options in this program.
1. -h Prints help text, shows user options and how to use the program
2. -v Enables verbose output, things like the total files searched and total matches found
3. -r Enables recursive search mode, if a dir is found, mgrep will search that dir as well
4. -c Enables cool colors. NOTE: It will not search for your .dircolors file or anything,
   these are colors I think are cool, with a black terminal background. If your terminal is
   bright, this will most likely look aweful and consider becoming a real programmer and
   changing your background to a dark color.
5. -n Adds an aditional newline between pattern finds. Default is 1 like grep
6. -l Prints the line number of the file the pattern was found
7. -s Prints the line of source cose that contained the pattern
8. -a Searches ALL files. A handful are skipped by default:
   - no extension
   - .so
   - .a
   - .o
   - .dll
   - .17git
   - .git
   - .db
   - .bin
   - .cmake
   - .png
   - .jpg
   - .pdf
Skipping these files make a large difference while searching my entire computer. ~12-11 seconds
down to ~4 seconds. The -a option mgrep will search EVERY file that the user has read permissions for.

## Using mgreps Full Power
I can say with extremely high certainty that whatever pattern I am looking for is not in any file 
that is skipped via the default search, so I will not be using -a, but I do want: 
- colors
- line numbers
- source code
- additional new lines
```bash
./mgrep -rclns "pattern" dir
```
## Timing 
This timing was done on various levels of my laptops directories. It should be known, as I stated in the introduction, grep does not search some files, and mgrep also does not search some files. So this timing is not precisely how long it takes these two programs to search exactly 100000 files, rather how long does it take each of the programs to do a certain search. Some files will be left out, and it is likely what I am looking for is not in either the files grep or mgrep leaves out.

Average speed for 10 tests using "time" unix command, except for the largest test where I averaged 
4 tests:

| Test Reps |Search Size|  mgrep -r | mgrep -ra | grep -rI | grep -r |
|-----------|-----------|-----------|-----------|----------|---------|
|     10    |    136    |  0.0087s  |  0.0091s  |  0.006s  | 0.0107s |
|     10    |  30754    |   0.218s  |   .2123s  | 0.1457s  | 1.0744s |
|     10    |  30881    |  0.5565s  |   .5373s  | 0.3127s  | 1.2158s |
|      4    | 581113    |  4.6043s  |  10.852s  |25.7978s  |37.6785s |

As you can see, for small searches and the -I flag on grep, which skips binary files, grep is faster. Impressively so, for the second and third sets of testing. I will be reading about some optimizations they have done to get grep to be so fast. However, when the search gets very large, that is when mgrep is already able to beat grep. As of these tests, I have not made any serious optimizations as of now, and hope to improve these times.

## Implementation Details



## What I Learned & Other
Last term I just began learning about multi-threading, using the mother tongue, C and the 
pthreads library. Including a few small programs to learn the C++ threads API, I had only used 
threads in a very static boring 
```cpp
while (num < 10000000) {
  ++num;
}
```
kind of a way. After reading about how a Real Time System would allocate work for a set of worker 
threads to do various "jobs", I wanted to do something similar. This is when I made the ThreadPool.
Originally it was its own program and it still just incremented a number to an arbitrary high value.
I copied that file and turned its functionality into a class. The API is still messy as far as a 
template of a thread pool, which is what I want. However, for this it works.

Another notable find was the std::filesystem library. I was surprised how easily I was able to get 
the format right, and how many options there were, A+ for the API. I do also feel myself getting 
better with CMake, slowly learning the options and how to set up the CMake_list.txt. Even the 
speed at which I can navigate obstacles is increasing as I do more of these projects. Ensuring 
nvim LSP can see the right .hpp files, random design choices or even algorithmic problems.




