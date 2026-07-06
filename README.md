# mgrep
This program is a multi-threaded grep, that essentially at this point is a slightly faster, 
less feature rich version of ripgrep.
  

## Overview
At this current point, mgrep is tied or faster than ripgrep for searching various paths in my 
own laptop, while searching more files. Obviously, it is not as robust or full of various 
features that mgrep currently lacks. This is part of the advantage of being a 10 year old 
open source software. Do not confuse my pride and interest in this project as an 
assertion that mgrep is a superior tool overall.

## Quick Start
### Dependencies
- CMake
- C++ Compiler


```bash
git clone https://github.com/Michael-Haring/mgrep.git
cd mgrep
```
From the project root you have 2 options
To build and move to your local/bin/ just run:
```bash
sudo make install
```
If you dont want it in your bin just yet, you can just build the binary:
```bash
make
```
Finally you can run the almighty mgrep...
```bash
./build-release/mgrep
```
If you would like to test the binary built, you can compile the tests and run them with:
```bash
make test
```
If you never ran the sudo make install, you can use it now to move it to your local/bin/ with:
```bash
sudo make install
```


## Options
Efforts have been made to include features that I have never once used, but that seasoned 
programmers might find useful, or features that various versions of grep may have already.
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
10. --type TYPE Only searches files in a named type: header, source, cpp.
11. --ext EXT[,EXT...] Only searches files with matching extensions. Example: --ext h,hpp
12. --glob GLOB Only searches paths matching GLOB. Example: --glob 'include/**/*.hpp'
13. --exclude-glob GLOB Skips paths matching GLOB. Example: --exclude-glob '*test*'
14. --heading Groups line/source output under each matching file path.
15. --files Lists files mgrep would search, without requiring a pattern.
16. --files-from FILE Reads newline-delimited input file paths from FILE
17. --files-from0 FILE / --null-files-from FILE Reads NUL-delimited input file paths from FILE
18. -t / --theme THEME Select a named color theme: blue, red, green, purple, cyan, yellow, 
orange, pink, mono, bright, gruvbox, nord, dracula, nebula.
19. --no-color Disables ANSI color output.
20. --literal Treats backslashes in the pattern literally. By default, pattern escapes 
like \n, \r, \t, \0, \\, and \xNN are decoded before searching.
21. --colors COMPONENT:ATTR:VALUE Overrides colors. Example: --colors match:fg:magenta
22. -n Adds an aditional newline between pattern finds. Default is 1 like grep
23. -l Prints the line number of the file the pattern was found
24. -s Prints the line of source cose that contained the pattern
25. -a Searches ALL files. A handful are skipped by default:
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

## Usage:
For general usage I like:
```bash
./mgrep -rslt gruvbox 
```
Or better, make an alias in your ~/.bashrc file
```
alias mg='mgrep -rslt gruvbox
```
## Timing 
Below I have the results of a script that times each search command with the time Unix/Linux 
command. The files opened were tracked with strace. The script and command was called from a 
location that aligned the ../../../../ search with my ~/. The times are measured in seconds.

| Command   |Search Root  |Files Opened| Avg Time |
|-----------|-------------|------------|----------|
| grep -rI  |       ../.. |   27348    | 0.477s   |
| mgrep -r  |       ../.. |     229    | 0.008s   |
| mgrep -rs |       ../.. |     229    | 0.009s   |
|    rg     |       ../.. |     206    | 0.011s   | 
| mgrep -r  |    ../../.. |     920    | 0.012s   |
| mgrep -rs |    ../../.. |     920    | 0.014s   |
|    rg     |    ../../.. |     762    | 0.017s   |
| mgrep -r  |../../../../ |    1120    | 0.013s   |
| mgrep -rs |../../../../ |    1120    | 0.016s   |
|    rg     |../../../../ |    1067    | 0.019s   |

### Time Conclusions
#### grep
as far as I know grep is much more difficult to get to not search a ton of files. the -I skips 
binary files and increases speed, but grep still has to open them. This makes these kinds of 
measurements more difficult. One thing can be said, grep is extremely efficient, single threaded 
and seemingly limited by system wait times. Grep is pretty remarkable.
#### mgrep vs ripgrep
