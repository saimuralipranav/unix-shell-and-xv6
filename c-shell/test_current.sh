#!/bin/bash

SHELL_BIN="$(pwd)/shell.out"
TEST_DIR="/tmp/cshell_test"

rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR/subdir/nested"

cat > "$TEST_DIR/test.txt" <<'FILE'
line one

line three
line four
FILE

cat > "$TEST_DIR/multi.txt" <<'FILE'
alpha
beta

gamma
FILE

cat > "$TEST_DIR/subdir/file.txt" <<'FILE'
inside subdir
second line
FILE

touch "$TEST_DIR/.hidden"

cd "$TEST_DIR" || exit 1

echo "============================================================"
echo " C-SHELL CURRENT TEST: A3 + HOP + REVEAL + PEEK"
echo "============================================================"

"$SHELL_BIN" <<'INPUT'

# A3 parsing
echo hello
echo "hello world"
echo "a > b"
echo my\ file
echo ""
echo "unclosed
echo trailing\

# HOP
hop ~
hop ..
hop -
hop ~ ..
hop -
hop /tmp/cshell_test
hop does_not_exist

# REVEAL
reveal
reveal -a
reveal -t
reveal -at
reveal -ta
reveal -x
reveal . .
reveal -a .

# PEEK
peek test.txt
peek -n test.txt
peek -r test.txt
peek -rn test.txt
peek -nr test.txt
peek -rrrrnnnn test.txt
peek test.txt multi.txt
peek -n test.txt multi.txt
peek -r test.txt multi.txt
peek does_not_exist
peek subdir
INPUT

echo
echo "============================================================"
echo " TEST FINISHED"
echo "============================================================"
echo "Test directory: $TEST_DIR"
