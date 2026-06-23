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

If that works and you want it to be callable, the same as grep, ripgrep, time, ls or any of these various commands at your disposal:
Put the mgrep executable in your /usr/local/bin. cp the executable into that dir. Assuming you are in the executables directory you could do:
```
sudo cp ./mgrep /usr/local/bin/mgrep
```
then type your password to gain permission to do this. After this, you should be able to call mgrep like any other linux command. Alias' can then be made in your ~/.bashrc file so a simple "mg" expands to something like mgrep -rclsn or whatever colors you want.

### Catch2 Test Suite
At this point, there are over 750 tests in the test suite. Don't look at me, it was the agent. The exact code touched % that the suite achieves is unknown at this point, but I will rectify that soon. The point is that the test suite is massive, and relatively speaking compared to my 12 test suite I had written, then stopped adding to because I am a nooby peasant. The agent has been adding all of these tests, to ensure lack of functional regression as new features or optimizations are implemented.

## Features
### ThreadPool Creation
This project started as an excuse to use a thread-pool I had made and thought was really cool, and a far more interesting use case than what school assignments had provided. Many changes have been made to this project on the path of optimizations. Originally, we started a pool with 75% of your maximum parallel running threads. Though it is cool, it is highly inefficient, particularly when paired with a small total search, and the implementation of batching the files. So for small searches (<80 files) the program runs serially, with a single thread.

### Batching of files and output
Originally, every file that was was pushed along with a read_file function. This is fine, and it worked pretty well. However it creates a lot of friction in your system, at least potentially, through blocking. Every push of a single file, block. Every pop of a single file, block. Every cout, block. In my programs case, this was a very real bottleneck. Allowing the original thread, which at the time was the lone worker doing the path traversal and work load pushing, to build up a decent workload, even if its just 32 files, before pushing and allowing threads to pick up real amounts of work improved the speed of the program.

Batching the output was a similar issue, and solved with a similar solution of batching the output, which also allows for the output of files to maintain their locality with other files being searched in the output. Ripgrep has mutilple threads collecting work to be done. You can tell this because the outputs are rather speratic, and lack that locality that you maintain with single-threaded work-pushing. However you can still maintain this with careful traversal and the batching of the outputs, with minimal added latency.

### Pruning
The greatest quote I have ever heard in the domain of program optimization is "The fastest code is the code that doesn't run". There are many versions of this quote, but this is the most concise. I had no idea how much more work was being done by mgrep originally until I used strace to track the total file opens each programw as doing. When I saw mgreps files were an order of magnitude above ripgrep, I did some digging and discovered mgrep was going down the most useless deep, paths that contained dozens of thousands of files. Removing this type of work from general searches, first though explicit hard-coded skips, then a mixture of the hard-coded skips and a customizable .ignore file that must live in the /$HOME dir. This does to some degree feel like a cheat. You arent doing anything more efficiently, you are simply skipping work that does not need to be done. It is not interesting or fancy, but it is a real optimization that should be kept in mind. I know the Creator of C++ said something like this on a podcast. "My favorite thing while programming is to see a snippet of code that looks beautiful, geniusly handwritten. Then to see how much faster the program runs without it.". This is paraphrased, but it is real.

### Modularity
This is a more nuanced optimization that I do not completely understand, but that makes it more interesting. I am a noob, still a junior at the point fo writing this. The read_file() function grew to such a size, with so many if elses scattered around, very similar code written time and time again. According to perf stats (very cool command), my program had far more branch misses than ripgrep. I have read about this stuff before. My essential understanding of this is that the compiler makes insane optimizations to your code. Not only this, but while executing, it tried to predict the next few instructions, which if it is correct, helps with fast execution even further. The more branchy your code is, the less predictable it is to the compiler, OS or hardware, the worse off your program will be for a combination of several factors including what I have just described. I have noticed real speedups in prior programs I have written, simply by crudely throwing blocks of code that seemed like they were a single function into functions. This optimization did improve the speed of the program, and reduced branch misses.

### Versions of read_file
Part of the this issue and the previous problem mentioned was that the read_file mathod had grown to be much more than a simple read_file function. It contained many read_file implementations that were each there to read a different files, search for different types of patterns, or even for different options used for the mgrep command. With the modularization of the program, meant the modularization of the read_file, which had grown to be the majority of the lines of code in the program. Now there is a function that determines the best read_file method based on the files stats, and options used, which allows for the read_file methods themselves to be slimmer, and far less branchy.

This optimization actually has a second win burried a bit under the surface, that might not be as obvious, or maybe its more obvious depending on the angle you are coming from. Modularizing the code in this way allows for the easy implementation of more versions of the read_file method. It makes expanding the program to be more feature rich easier. This is not to be understimated, particularly when an agent is working on the codebase.


## Options
Efforts have been made to include features that I have never once used, but that seasoned programmers might find useful, or features that various versions of grep may have already.
The legendary getopt provides us with several options in this program
1. -h Prints help text, shows user options and how to use the program
2. -v / --invert-match Prints lines that do not contain the pattern
3. -i / --ignore-case Matches ASCII letters case-insensitively
4. --verbose Enables verbose output, things like the total files searched and total matches found
5. -r Enables recursive search mode, if a dir is found, mgrep will search that dir as well
6. -p / --pretty Compatibility alias; colors are enabled by default.
7. -c Prints matching line counts instead of normal matches
8. -q Prints nothing, only returns match status
9. -o Prints only matching text, one occurrence per line
10. --files-from FILE Reads newline-delimited input file paths from FILE
11. --files-from0 FILE / --null-files-from FILE Reads NUL-delimited input file paths from FILE
12. -t / --theme THEME Select a named color theme: blue, red, green, purple, cyan, yellow, orange, pink, mono, bright.
13. --no-color Disables ANSI color output.
14. --colors COMPONENT:ATTR:VALUE Overrides colors. Example: --colors match:fg:magenta
15. -n Adds an aditional newline between pattern finds. Default is 1 like grep
16. -l Prints the line number of the file the pattern was found
17. -s Prints the line of source cose that contained the pattern
18. -a Searches ALL files. A handful are skipped by default:
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
./mgrep -rslnc --colors file:fg:yellow --colors line:fg:3 --colors source:fg:244 --colors match:fg:55'
```
Or better, make an alias in your ~/.bashrc file
```
alias mg='mgrep -rslnc --colors file:fg:yellow --colors line:fg:3 --colors source:fg:244 --colors match:fg:55'
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
