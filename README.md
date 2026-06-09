# mgrep
  This program is a multi-threaded grep, that essentially at this point is a slightly faster, less feature rich version of ripgrep.
  

## Overview
At this current point, mgrep is tied or faster than ripgrep for searching various paths in my own laptop, while searching more files. Obviously, it is not as robust or full of various features that mgrep currently lacks. This is part of the advantage of being a 10 year old open source software. Do not confuse my pride and interest in this project as an assertion that mgrep is a superior tool overall.

## Agentic AI
I have had several experiences now where employers express desire for their employees to have experience working with agentic AI models. In an attempt to familiarize myself with this new technology and try to evolve with the times I used agentic AI on this program. I have a 20$/month OpenAI subscription which gives me access to OpenAI's Codex model. If you are unfamiliar with this tool, it is essentially a ChatGPT that lives in your terminal, that is more tuned towards programming, rather than delivering pages and pages of text.

I did not start the project with this tool, and this is my first time using anything like this, so beginning a new project from scratch with the help of AI might bring along with it several unique challenges I did not have to overcome. By the time I was using Codex on this problem, I already have a well functioning relatively fast mgrep, with a couple of required options.

When I had decided to use Codex, I began researching how to use these models effectively. I learned about a harness, and these *.md files you can use to keep certain ideas at the forefront of the models "thinking", or essentially use the .md files as scripts that the agent can look and and begin executing. You can give it various rules in the agents equivelent to a AGENTS.md, either locally or at your /$HOME. With all of these features at your disposal, you can really narrow the models focus, and get results that were, at least my pea sized brain, absolutely mind blowing.

### Agentic AI Subjective
A good arument you often here is a steel-manned version of the AI is bad, trained on bad code, unreliable results or hallucinations. Even, changing code that was irrelevent to what you asked. Any horror story you could imagine. I was extremely hesitent to even activate the model as I did not trust it to not just begin removing files off of my computer and other such nonsense. I did not even want to use codex on my original project incase it completely destroyed it. I copied the entire project to basically give codex a playground. When I first started Codex in this projects root, I already had a bash script that ran mgrep against other greps several times and collected benchmarks. I already had a Catch2 test suite.

I asked codex, to look inside the src/ dir and list what the top 5 most effective optimizations we could make were. After telling the agent about all of the tools it had at its disposal, it began working to implement those optimizations. testing the effects of the changes, and adding test cases to the script as it went. Adjusting old tests that failed because of new ways of doing things. It was not magic, it was just able to try all of these various ways of doing things very quickly, and improve on iterations based on parameters I gave, namely functionality and speed.

I am not an expert and this is not an article centered around philosophy, so I will not drag on here but the point is, Codex is an extremely powerful tool. Times are changing, and the current version of Codex is very likely the worst version you will ever use again.

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
Efforts have been made to include features that I have never once used, but that seasoned programmers might find useful, or features that various versions of grep may have already.
The legendary getopt provides us with several options in this program
1. -h Prints help text, shows user options and how to use the program
2. -v / --invert-match Prints lines that do not contain the pattern
3. --verbose Enables verbose output, things like the total files searched and total matches found
4. -r Enables recursive search mode, if a dir is found, mgrep will search that dir as well
5. -p Enables cool colors
6. -c Prints matching line counts instead of normal matches
7. -q Prints nothing, only returns match status
8. -o Prints only matching text, one occurrence per line
9. --files-from FILE Reads newline-delimited input file paths from FILE
10. --files-from0 FILE / --null-files-from FILE Reads NUL-delimited input file paths from FILE
11. -n Adds an aditional newline between pattern finds. Default is 1 like grep
12. -l Prints the line number of the file the pattern was found
13. -s Prints the line of source cose that contained the pattern
14. -a Searches ALL files. A handful are skipped by default:
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


# Below this will be updated when I am ready.

## Using mgreps Full Power
I can say with extremely high certainty that whatever pattern I am looking for is not in any file 
that is skipped via the default search, so I will not be using -a, but I do want: 
- colors
- line numbers
- source code
- additional new lines
```bash
./mgrep -rplns "pattern" dir
```
## Timing 
This timing is not the most precise set of measurements known to mankind, so take them with a grain of salt.
The script starts in the project root, and backs up one directory every search iteration. I also stopped timing against grep as mgrep and rg both blow grep out of the water (multi-threading and pruning win, grep is ultimitely efficient).
A more interesting test, at least currently without a fair test to use with grep, is against ripgrep. Just like ripgrep, mgrep skips files denoted in a .ignore file in your users home directory. I use an strace call to determine the approximate number of files opened by each program. 

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
