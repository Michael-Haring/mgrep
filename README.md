# mgrep
  My multi-threaded version of the unix classic, grep. I was recently playing 
around with threads and made a thread pooling system, but struggled to find meaningful 
tasks to give to the threads to really put it to the test. A few days later I heard 
that grep was single threaded and I knew then I had to make my own multi-threaded version, 
and that searching a file for a pattern would make for a solid task.


I hope to slowly work on aspects of this program that grep still beats. Mostly simple features 
it would be cool to implement some faster reading and actual a framework to get real benchmarks.
Also once that framework is setup, I would like to also test against another multi-threaded 
version of grep like ripgrep.

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


The testing script is basically empty
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

## Using mgreps Full Power
I want:
- colors
- line numbers
- source code
- additional new lines
```bash
./mgrep -rclns "pattern" dir
```
## Timing 
This should be updated soon, these are simply done using the real time provided by the 
time unix command. These tests were conducted in various directories of my laptop.
The largest test is my home directory, which includes Unity and a tons of VERY large files.

| Test Size |  grep  | mgrep  |
|-----------|--------|--------|
| 118       | 0.016s | 0.008s |
| 30734     | 1.059s | 0.224s |
| 580959    |38.284s |10.997s |
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




