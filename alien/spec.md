alien — Language & Interpreter Spec (v0.1)
🎯 Goals / Overview

Purpose: provide a minimal, BASIC-inspired scripting language for our proof-of-concept OS (user-space only).

Mode: command-line tool alien. Running alien some_script.aln executes the script. (REPL mode is deferred for now — first target is script execution.)

Design philosophy: simple, easy to learn, fun to toy with — reminiscent of old microcomputer BASIC or DOS-era scripting, not a full modern language.

🔤 Language Features (v0.1)

Numeric type: all numbers are floating-point (double precision).

Variables: single-letter variable names (A, B, ..., Z).

Arithmetic / Expressions: support +, -, *, /, and parentheses for grouping.

Assignment: e.g. A = (3 + 4) * 2; — semicolon-terminated.

Output: print(expr); — print numeric expression to console.

Input: a simple input(var); — read a number from keyboard into variable. Enables scripts like “enter your name or number.”

Control flow:

if (expr) { ... } — if expression evaluates non-zero, execute block.

while (expr) { ... } — loop while expression is non-zero.

Block syntax with { ... }.

Semantics: script runs top-to-bottom; variables persist across statements; simple, linear control flow with structured branching/looping; no GOTO, no line numbers.

📄 Example Script (v0.1)
// simple.aln
print( 2 + 2 );
A = 0;
while (A < 5) {
    print(A);
    A = A + 1;
}

print("Enter a number: ");  // (see note below)
input(A);
print(A * 2.5);

Note: for now print(expr) is limited to numeric expressions — string printing is not yet supported in v0.1. (If you want string support later we can expand.)

🛠 Interpreter Behavior & Parser Rules

Statement-based syntax; each statement ends with semicolon.

Parser: a recursive-descent parser for expressions + statements. Good for small/simple grammars and avoids heavy dependencies. This matches the classic “interpreter pattern.”
Medium
+2
Ruslan's Blog
+2

Variable storage: a simple symbol table (e.g. array or small fixed-size table for 26 variables).

Input I/O: use standard input (keyboard) and standard output (console) — for example, reading via fgets() or scanf()-like approach, writing via printf().
W3Schools
+2
tutorialspoint.com
+2

Error handling: detect syntax errors and runtime errors (e.g. malformed syntax, unknown token, missing semicolon, undefined variable, invalid input for input()), report approximate line number + error hint.

✅ What We Intentionally Leave Out (for v0.1)

Strings (beyond maybe bare minimum, if at all) — no string type or string manipulation (for simplicity).

Arrays, functions, modules, scoping beyond global variables.

File I/O, OS-level APIs, concurrency, sandboxing.

REPL interactive mode.

GOTO / line numbers: structured control only (if/while/blocks).

🧠 Rationale & Tradeoffs

Using floating-point numbers by default gives flexibility (non-integer math) — overhead is modest in modern hardware/VM context.

Single-letter variables are simple to parse, reduce symbol-table complexity, and feel nostalgic (like old BASIC/Tiny BASIC). Also matches minimalist spirit. Indeed, classic minimal dialects of BASIC used one-letter variable names.
Wikipedia
+1

Semicolon-terminated statements keep grammar simple and unambiguous — easier to write a recursive-descent parser reliably.

Structured control flow instead of GOTO avoids spaghetti-code, simplifies interpreter design, reduces pitfalls in parsing and execution (especially around error detection and stack/flow control).

Minimal I/O (print, input) provides essential interactivity without complicating interpreter or runtime environment.

Given this lean core, the interpreter should remain small, easy to compile statically, and robust enough for educational / toy-project scripting.

🔧 Implementation Notes (C-based interpreter)

Use standard C I/O (stdin, stdout) to implement input() (e.g. via fgets() or simple scanf-style parsing) — that’s typical and supported cross-platform.
W3Schools
+1

Use a simple fixed-size table (e.g. array of 26 elements) for variables A–Z — store double values.

Lexer → parser → AST or direct evaluation design (e.g. recursive-descent) — this is a common minimal interpreter architecture.
Medium
+1

For error reporting: track line numbers (increment on newline detection during lexing), and include line number in error messages (syntax error, unexpected token, missing semicolon, runtime error, etc.).

🗂 Next Steps

Use this spec as “v0.1 canonical reference” for alien.

Build/adjust the C-interpreter skeleton accordingly (update the earlier skeleton we wrote). Add input(...) support.

Write a small set of example .aln scripts (hello world, read-calc-print, simple loop/demo).

Test thoroughly: valid scripts, invalid scripts (syntax errors, missing semicolons), edge cases (division by zero, input errors), to verify error reporting works.

Review compiled binary size and dependencies — ensure alien remains lightweight for OS bundling.
