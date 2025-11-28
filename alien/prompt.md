You are the lead C developer for our toy OS project (AIOS).  

We want to build a minimal scripting language called "alien" (file extension `.aln`) to bundle with the OS. Spec v0.1:  

first read our AI agent SOP at https://raw.githubusercontent.com/b3p3k0/configs/refs/heads/main/AI_AGENT_FIELD_GUIDE.md

- identifiers may be multi-character (letters/underscores followed by letters/digits/underscores) and hold either numeric or string values  
- arithmetic expressions: +, -, *, /, parentheses (numeric-only)  
- assignment statements with semicolon: e.g. `score = 3.14;` or `alias = "ALN";`  
- `write(expr1, expr2, ...);` to output mixed numeric/string expressions with automatic newline; `+` concatenates strings (and coerces numbers to strings when needed)  
- `read(X);` to read a number from keyboard into variable X  
- control flow: `if (expr) { ... }` and `while (expr) { ... }`  
- blocks with `{ ... }` and semicolon-terminated statements; no GOTO, arrays, advanced types  
- script mode only (no REPL for now) — usage: `alien script.aln`  
- error reporting should include approximate line number and an error message when encountering syntax errors (unexpected token, missing semicolon, invalid identifier, etc.)  

Please produce a full C implementation file (`alien.c`) that:  

- compiles with `gcc -std=c99 -o alien alien.c -lm` (only libc + math)  
- reads the script file from argv[1], lexes, parses, builds an AST or otherwise evaluates, then executes script top-to-bottom  
- supports `read()` and `write()` per spec (including string literals), variable store, arithmetic, control flow  
- includes basic error handling and prints helpful error messages including line number on syntax or runtime error  
- includes minimal comments/documentation in code  

After that, produce 3 example `.aln` scripts:  

1. `hello.aln`: writes a welcome message with strings, asks for a number using read(), then writes something using that number  
2. `loop.aln`: demonstrates while loop and write — e.g. count from 1 to 10  
3. `calc.aln`: demonstrates arithmetic expressions and assignment — e.g. compute quadratic formula (or something simple)  

Wrap your response as code blocks and plain text so it’s copy-paste ready.  

BEFORE YOU WRITE CODE we will have a dialog to flesh out the details; the above instructions describe what I want you to do; before you begin we need to plan how we will do them.

please ask me any questions as we begin!
