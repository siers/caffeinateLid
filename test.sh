#!/bin/bash
B=./caffeinateLid; S=$PWD/state; export CAFFEINATELID_DRY_RUN=$S
fail=0; check(){ got=$(cat $S); [ "$got" = "$2" ] && echo "PASS $1" || { echo "FAIL $1 (got $got want $2)"; fail=1; }; }
mid(){ for i in $(seq 30); do [ "$(cat $S)" = 1 ] && break; sleep 0.1; done; check "$1 during" 1; }

echo 0 > $S; timeout 1 $B 2>/dev/null & mid timeout; wait; check "timeout after" 0
echo 0 > $S; $B 2>err & p=$!; mid SIGINT; kill -INT $p; wait $p; rc=$?; check "SIGINT after" 0
if [ $rc = 0 ] && grep -q "restored disablesleep 0" err && ! grep -q FAILED err; then echo "PASS SIGINT clean exit"; else echo "FAIL SIGINT clean exit (rc=$rc)"; cat err; fail=1; fi
echo 0 > $S; $B 2>/dev/null & p=$!; mid SIGHUP; kill -HUP $p; wait $p; check "SIGHUP after" 0
echo 0 > $S; $B 2>/dev/null & p=$!; mid SIGKILL; kill -KILL $p; wait $p 2>/dev/null; sleep 0.3; check "SIGKILL after" 0
echo 0 > $S; timeout -k 0.2 1 $B 2>/dev/null & mid "timeout -k"; wait; check "timeout -k after" 0
# kill the whole process group repeatedly (simulates mashing Ctrl-C)
echo 0 > $S; setsid=$(command -v setsid); bash -c "$B 2>/dev/null & sleep 0.3; for i in 1 2 3 4 5; do kill -INT 0 2>/dev/null; done; wait" ; sleep 0.3; check "group INT spam after" 0
echo 1 > $S; timeout 1 $B 2>/dev/null; check "previously-1 stays" 1
echo 0 > $S; $B extra 2>/dev/null; [ $? = 2 ] && check "bad args untouched" 0
exit $fail
