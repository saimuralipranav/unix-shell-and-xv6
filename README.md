# C-Shell (Parts A-C)

A custom POSIX-compliant interactive shell in C supporting tokenizer parsing, directory hopping, listings, file viewing, and redirections/piping.

## Project Structure
* `c-shell/src/`: C implementation files.
* `c-shell/include/`: C header files.
* `c-shell/Makefile`: Multi-stage compilation rules.

## Build & Run
To compile:
```bash
cd c-shell/
make all
```
Produces `shell.out`.

To run:
```bash
./shell.out
```

To clean:
```bash
make clean
```
