# Development
This program was initially developed during my Spring 2026 term at PSU. I had just learned about threads, and wanted to do something cool with them.

## Origin
mgrep was originally just a reason to use a thread pooling system I had created. This 
program was enough work to really put the thread pool to work, rather than counting to a million or other less exciting tasks. Eventually, I had a working "grep clone" that was slower than grep -I for small and medium searches, but faster for the largest search.

## grep
At this point, I really had no idea what I was measuring, I was happy my program was 
working and wanted to get mgrep to be faster than grep, which at the time, seemed like 
it should be trivial as grep is single threaded after all. So regardless, I have so 
much more power at my disposal, I should be able to leverage that and get faster 
times without being a wizard. The problem with that is that grep is optimized beyond your wildest dreams. Decades of wizards tweaking lines of code and ways of doing things. Grep is a relic from a time when people squeezed every ounce of performance out of their 
hardware. The real reason grep was so slow is that its opening every single file in 
the path. grep is so efficient its pretty unbelievable.

## ripgrep
Now, ripgrep on the other hand makes it very easy to skip many files, with the .ignore 
semantics. Ripgrep is extremely fast, with parallelized directory traversal, and all of the bells and whistles. 

## Tool V.S Project

The final main difference, and the last one I noticed while developing mgrep, was the insane difference between a fun project, and a real tool that is depended on by many 
other individuals, companies, or other tools. The difference between, what I need or have needed in the past, as a nooby student, and the plethora of options and features programs like grep or ripgrep offer users. Not to mention the reliability and dependability of these programs. They do not fail, they always return useful information, options for different types of outputs, piping functionality. Some features are more obvious than others depending on your usual workflow with similar tools, things like a file list of files to search, and the .ignore file instead of just hardskipping all of the files and dirs I want to skip. These are just a small handful of thing that jump out at you as work to be done, when a project is transitioning into a real tool. 0-350 lines, mgrep was a project, and it worked, not well, but it worked. At the time of writing this, mgrpe has grown to almost 2k lines of code, This doesnt sound too large if you just imagine it as 4 classes all inheriting the same easy functions, that just take up tons of space. There 
are about 1k lines of pretty decent read logic for various combinations of options and depending on what the user wants returned.


## Agentic AI
I have had several experiences now where employers express desire for their employees to have experience working with agentic AI models. In an attempt to familiarize myself with this new technology and try to evolve with the times I used agentic AI on this program. I have a 20$/month OpenAI subscription which gives me access to OpenAI's Codex model. If you are unfamiliar with this tool, it is essentially a ChatGPT that lives in your terminal, that is more tuned towards programming, rather than delivering pages and pages of text.

I did not start the project with this tool, and this is my first time using anything like this, so beginning a new project from scratch with the help of AI might bring along with it several unique challenges I did not have to overcome. By the time I was using Codex on this problem, I already have a well functioning relatively fast mgrep, with a couple of required options.

When I had decided to use Codex, I began researching how to use these models effectively. I learned about a harness, and these *.md files you can use to keep certain ideas at the forefront of the models "thinking", or essentially use the .md files as scripts that the agent can look and and begin executing. You can give it various rules in the agents equivelent to a AGENTS.md, either locally or at your /$HOME. With all of these features at your disposal, you can really narrow the models focus, and get results that were, at least my pea sized brain, absolutely mind blowing.

### Agentic AI Subjective
A good arument you often here is a steel-manned version of the AI is bad, trained on bad code, unreliable results or hallucinations. Even, changing code that was irrelevent to what you asked. Any horror story you could imagine. I was extremely hesitent to even activate the model as I did not trust it to not just begin removing files off of my computer and other such nonsense. I did not even want to use codex on my original project incase it completely destroyed it. I copied the entire project to basically give codex a playground. When I first started Codex in this projects root, I already had a bash script that ran mgrep against other greps several times and collected benchmarks. I already had a Catch2 test suite.

I asked codex, to look inside the src/ dir and list what the top 5 most effective optimizations we could make were. After telling the agent about all of the tools it had at its disposal, it began working to implement those optimizations. testing the effects of the changes, and adding test cases to the script as it went. Adjusting old tests that failed because of new ways of doing things. It was not magic, it was just able to try all of these various ways of doing things very quickly, and improve on iterations based on parameters I gave, namely functionality and speed.

I am not an expert and this is not an article centered around philosophy, so I will not drag on here but the point is, Codex is an extremely powerful tool. Times are changing, and the current version of Codex is very likely the worst version you will ever use again.


### Catch2 Test Suite
At this point, there are over 750 tests in the test suite. Don't look at me, it was the agent. The exact code touched % that the suite achieves is unknown at this point, but I will rectify that soon. The point is that the test suite is massive, and relatively speaking compared to my 12 test suite I had written, then stopped adding to because I am a nooby peasant. The agent has been adding all of these tests, to ensure lack of functional regression as new features or optimizations are implemented.

