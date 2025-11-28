alien — Language & Interpreter Spec (v0.1)
🎯 Goals / Overview

Purpose: provide a minimal, BASIC-inspired scripting language for our proof-of-concept OS (user-space only).

Mode: command-line tool alien. Running alien some_script.aln executes the script. (REPL mode is deferred for now — first target is script execution.)

Design philosophy: simple, easy to learn, fun to toy with — reminiscent of old microcomputer BASIC or DOS-era scripting, not a full modern language.

🔤 Language Features (v0.1)

Numeric type: all numbers are floating-point (double precision).

Variables: identifiers may be any letter/underscore followed by letters, digits, or underscores (case-sensitive). A single namespace stores both numeric and string values.

Arithmetic / Expressions: support +, -, *, /, and parentheses for grouping.

Assignment: e.g. A = (3 + 4) * 2; — semicolon-terminated.

Output: write(expr1, expr2, ...); — emit a comma-separated list of expressions followed by a newline. Numbers format to three decimal places, strings print verbatim, and the `+` operator concatenates adjoining strings/numbers before printing.

Input: read(var); — read a number from keyboard into a variable (still numeric-only). Scripts typically pair a `write` prompt with a `read` call for interaction.

Strings: double-quoted string literals can be assigned to variables, concatenated with `+`, and compared with the standard relational operators (==, !=, <, <=, >, >=). Mixing string and numeric operands outside of `+` raises an error.

Control flow:

if (expr) { ... } — if expression evaluates non-zero, execute block.

while (expr) { ... } — loop while expression is non-zero.

Block syntax with { ... }.

Semantics: script runs top-to-bottom; variables persist across statements; simple, linear control flow with structured branching/looping; no GOTO, no line numbers.

📄 Example Script (v0.1)
// simple.aln
greeting = "Hello from alien, ";
user = "pilot";
write(greeting + user + "!");

write("Enter a number:");
read(value);
write("You typed ", value, ". Tripled that's ", value * 3, ".");

🛠 Interpreter Behavior & Parser Rules

Statement-based syntax; each statement ends with semicolon.

Parser: a recursive-descent parser for expressions + statements. Good for small/simple grammars and avoids heavy dependencies. This matches the classic “interpreter pattern.”
Medium
+2
Ruslan's Blog
+2

Variable storage: a simple global symbol table keyed by identifier strings.

Input I/O: use standard input (keyboard) and standard output (console) — e.g., `write()` formatting via printf() and `read()` parsing via fgets()/strtod.
W3Schools
+2
tutorialspoint.com
+2

Error handling: detect syntax errors and runtime errors (e.g. malformed syntax, unknown token, missing semicolon, undefined variable, invalid data for read()), report approximate line number + error hint.

✅ What We Intentionally Leave Out (for v0.1)

String support stays minimal: literals + assignment + concatenation/comparison only. No slicing, substring search, or string input yet (text input remains numeric-only via read()).

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

Minimal I/O (write/read) provides essential interactivity without complicating interpreter or runtime environment, while basic strings make prompts friendlier.

Given this lean core, the interpreter should remain small, easy to compile statically, and robust enough for educational / toy-project scripting.

🔧 Implementation Notes (C-based interpreter)

Use standard C I/O (stdin, stdout) to implement read()/write() (e.g. fgets()+strtod for input, printf for output) — lightweight and portable.
W3Schools
+1

Use a simple fixed-size table (e.g. array of 26 elements) for variables A–Z — store double values.

Lexer → parser → AST or direct evaluation design (e.g. recursive-descent) — this is a common minimal interpreter architecture.
Medium
+1

For error reporting: track line numbers (increment on newline detection during lexing), and include line number in error messages (syntax error, unexpected token, missing semicolon, runtime error, etc.).

🗂 Next Steps

Use this spec as “v0.1 canonical reference” for alien.

Build/adjust the C-interpreter skeleton accordingly (update the earlier skeleton we wrote). Ensure read()/write() (with string literals, variables, and concatenation) are implemented.

Write a small set of example .aln scripts (hello world with strings, read/write demo, simple loop).

Test thoroughly: valid scripts, invalid scripts (syntax errors, missing semicolons), edge cases (division by zero, read/input errors), to verify error reporting works.

Review compiled binary size and dependencies — ensure alien remains lightweight for OS bundling.
