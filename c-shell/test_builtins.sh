#!/bin/bash

set -u

SHELL="./shell.out"
TESTDIR="/tmp/cshell_test"

rm -rf "$TESTDIR"
mkdir -p "$TESTDIR/subdir/deep"

# ------------------------------------------------------------
# Test files
# ------------------------------------------------------------

cat > "$TESTDIR/file1.txt" <<'EOF'
line one

line three
line four
EOF

cat > "$TESTDIR/file2.txt" <<'EOF'
alpha
beta

gamma
EOF

cat > "$TESTDIR/subdir/file3.txt" <<'EOF'
sub one
sub two
EOF

cat > "$TESTDIR/subdir/deep/file4.txt" <<'EOF'
deep one
deep two
EOF

printf 'hidden\n' > "$TESTDIR/.hidden"

# ------------------------------------------------------------
# Run shell with a batch of commands
# ------------------------------------------------------------

cat > "$TESTDIR/commands.txt" <<EOF
reveal
reveal -a
reveal -t
reveal -at
reveal -ta
reveal -aaaa
reveal -tttt

hop .
hop ~
hop ..
hop -
hop $TESTDIR
hop -
hop /tmp
hop $TESTDIR
hop does_not_exist

peek $TESTDIR/file1.txt
peek -n $TESTDIR/file1.txt
peek -r $TESTDIR/file1.txt
peek -rn $TESTDIR/file1.txt
peek -nr $TESTDIR/file2.txt
peek $TESTDIR/file1.txt $TESTDIR/file2.txt
peek -n $TESTDIR/file1.txt $TESTDIR/file2.txt
peek -r $TESTDIR/file1.txt $TESTDIR/file2.txt
peek does_not_exist
peek $TESTDIR
peek -

locate ls
locate ls cat
locate does_not_exist
locate

reveal -x
reveal .
reveal . .
reveal -a .
reveal -t .
reveal -at .

hop ~ -
hop ~ .. -
hop $TESTDIR subdir
hop subdir
hop does_not_exist

EOF

# ------------------------------------------------------------
# Run
# ------------------------------------------------------------

echo "============================================================"
echo " Running C-Shell automated test batch"
echo "============================================================"
echo

"$SHELL" < "$TESTDIR/commands.txt" > "$TESTDIR/output.txt" 2>&1

cat "$TESTDIR/output.txt"

echo
echo "============================================================"
echo " Test batch finished"
echo " Output saved to:"
echo " $TESTDIR/output.txt"
echo "============================================================"

# ------------------------------------------------------------
# Basic expected-error checks
# ------------------------------------------------------------

echo
echo "Checking required error messages..."

FAIL=0

check_error() {
    PATTERN="$1"
    DESCRIPTION="$2"

    if grep -Fq "$PATTERN" "$TESTDIR/output.txt"; then
        echo "PASS: $DESCRIPTION"
    else
        echo "FAIL: $DESCRIPTION"
        FAIL=1
    fi
}

check_error "reveal: invalid syntax" \
    "reveal invalid syntax"

check_error "peek: no such file or directory" \
    "peek missing file"

check_error "peek: is a directory" \
    "peek directory"

check_error "locate: invalid syntax" \
    "locate without arguments"

check_error "locate: command not found (does_not_exist)" \
    "locate missing command"

check_error "hop: no such directory" \
    "hop missing directory"

echo

if [ "$FAIL" -eq 0 ]; then
    echo "============================================================"
    echo " BASIC ERROR-CHECKS: PASS"
    echo "============================================================"
else
    echo "============================================================"
    echo " BASIC ERROR-CHECKS: SOME TESTS FAILED"
    echo "============================================================"
fi

echo
echo "Full output:"
echo "$TESTDIR/output.txt"
