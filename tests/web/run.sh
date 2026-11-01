#!/bin/sh
# tests/web/run.sh — web runtime + gross tests. Run from anywhere after `make`.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT" || exit 1
CC=${CC:-cc}
B=build/tests/web
mkdir -p "$B"
TMP=$(mktemp -d)
PIDS=""
cleanup() {
    for p in $PIDS; do kill "$p" 2>/dev/null; done
    for p in $PIDS; do wait "$p" 2>/dev/null; done
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

passed=0
failed=0
ok() { passed=$((passed + 1)); }
bad() { failed=$((failed + 1)); echo "FAIL: $*" >&2; }
# check DESC COMMAND...: pass when the command succeeds
check() {
    desc=$1
    shift
    if "$@"; then ok; else bad "$desc"; fi
}
has() { grep -qF -- "$2" "$1"; }
hasnt() { ! grep -qF -- "$2" "$1"; }

[ -f build/lib/libioi.a ] && [ -x build/gross ] || { echo "run make first" >&2; exit 1; }

# ---------------------------------------------------------------- build
CFLAGS_T="-O1 -g -Wall -Wextra -Wno-unused-parameter -Iinclude -Iruntime/web"
$CC $CFLAGS_T tests/web/unit.c build/lib/libioi.a -lm -o $B/unit || exit 1
$CC $CFLAGS_T tests/web/app/notes.c build/lib/libioi.a -lm -o $B/notes || exit 1
$CC -O1 tests/web/rawsend.c -o $B/rawsend || exit 1
$CC $CFLAGS_T tests/web/dbopen.c build/lib/libioi.a -lm -o $B/dbopen || exit 1

# ---------------------------------------------------------------- unit
mkdir -p "$TMP/unit"
if $B/unit "$TMP/unit"; then ok; else bad "C unit tests"; fi

# ---------------------------------------------------------------- first open
# The forum's schema + seed (plaintext PASSWORD seeds get hashed) must set up
# a fresh database well inside gross's time limit.
if [ -f forum/schema.sql ]; then
    F="$TMP/forum.db"
    out=$(IOI_DATABASE=$F IOI_APP_DIR=forum $B/dbopen)
    ms=${out##*ms=}
    echo "forum fresh open: $out"
    check "forum fresh open succeeds" [ -n "$ms" ]
    check "forum fresh open under 500 ms (took ${ms} ms)" [ "${ms:-99999}" -lt 500 ]
    check "forum seed passwords hashed" [ "$(sqlite3 $F "SELECT count(*) FROM users WHERE password NOT LIKE 'pbkdf2-sha256\$%'")" = 0 ]
    full=$(sqlite3 $F 'SELECT count(*) FROM sqlite_master')
    # A setup killed part-way leaves nothing behind, and the next open redoes it.
    for delay in 0.01 0.03 0.06; do
        rm -f "$F" "$F-wal" "$F-shm"
        IOI_DATABASE=$F IOI_APP_DIR=forum $B/dbopen >/dev/null &
        kp=$!
        sleep $delay
        kill -9 $kp 2>/dev/null
        wait $kp 2>/dev/null
        n=$(sqlite3 $F 'SELECT count(*) FROM sqlite_master')
        check "setup killed after ${delay}s is all or nothing ($n of $full)" [ "$n" = 0 -o "$n" = "$full" ]
        if [ "$n" = "$full" ]; then
            check "complete setup has hashed passwords" [ "$(sqlite3 $F "SELECT count(*) FROM users WHERE password NOT LIKE 'pbkdf2-sha256\$%'")" = 0 ]
        fi
        again=$(IOI_DATABASE=$F IOI_APP_DIR=forum $B/dbopen)
        check "open after a killed setup completes it" [ "${again%% ms=*}" = "${out%% ms=*}" ]
    done
fi

# ---------------------------------------------------------------- gross
# start_gross DIR PROG [ENV...]: runs gross in DIR on a free random port with
# the options in $GROSS_ARGS, sets PORT. Retries on port collisions.
start_gross() {
    dir=$1
    prog=$2
    shift 2
    tries=0
    while [ $tries -lt 20 ]; do
        tries=$((tries + 1))
        PORT=$(( ( $(od -An -N2 -tu2 /dev/urandom) % 10000 ) + 20000 ))
        # shellcheck disable=SC2086
        (cd "$dir" && exec env "$@" "$ROOT/build/gross" -p "$PORT" -d "$dir/public" -b 127.0.0.1 \
            $GROSS_ARGS "$prog") 2>>"$dir/gross.log" &
        GPID=$!
        i=0
        while [ $i -lt 50 ]; do
            if curl -s -o /dev/null "http://127.0.0.1:$PORT/public/" 2>/dev/null; then
                PIDS="$PIDS $GPID"
                return 0
            fi
            kill -0 $GPID 2>/dev/null || break
            sleep 0.1
            i=$((i + 1))
        done
        kill $GPID 2>/dev/null
        wait $GPID 2>/dev/null
    done
    echo "could not start gross" >&2
    exit 1
}

G="$TMP/g"
mkdir -p "$G/public/sub" "$G/outside"
printf 'body { color: navy }\n' >"$G/public/style.css"
printf 'hello\n' >"$G/public/sub/a.txt"
printf 'TOP SECRET\n' >"$G/outside/secret.txt"
ln -s "$G/outside/secret.txt" "$G/public/link.txt"
GROSS_ARGS="-t 2 -l 3"
start_gross "$G" "$ROOT/tests/web/cgi.sh" IOI_DATABASE= IOI_APP_DIR=
U="http://127.0.0.1:$PORT"
R="$TMP/resp"

# CGI environment
code=$(curl -s -o $R.b -D $R.h -w '%{http_code}' "$U/hello/w%C3%B6rld?x=1&y=%20" -H 'Cookie: a=1')
check "cgi 200" [ "$code" = 200 ]
check "cgi method" has $R.b "method=GET"
check "cgi decoded PATH_INFO" has $R.b "path=/hello/wörld"
check "cgi raw QUERY_STRING" has $R.b "query=x=1&y=%20"
check "cgi host" has $R.b "host=127.0.0.1:$PORT"
check "cgi cookie" has $R.b "cookie=a=1"
check "cgi remote" has $R.b "remote=127.0.0.1"
check "cgi secret is 64 hex" has $R.b "secret=64"
check "cgi database default" has $R.b "database=$G/cassette.db"
check "cgi app dir default" has $R.b "appdir=$ROOT/tests/web"
check "content-type passthrough" has $R.h "Content-Type: text/plain"
check "no CONTENT_LENGTH without a body" grep -qx 'length=' $R.b
check "keep-alive header" has $R.h "Connection: keep-alive"
check "secret file mode 0600" [ "$(stat -c %a "$G/.gross_secret")" = 600 ]
check "access log line" grep -q '"GET /hello/w%C3%B6rld?x=1&y=%20 HTTP/1.1" 200' "$G/gross.log"

# keep-alive: two requests on one connection reach the same connection process
curl -s "$U/one" "$U/two" >$R.b
check "keep-alive reuses the connection" \
    [ "$(grep -c '^pid=' $R.b)" = 2 -a "$(grep '^pid=' $R.b | sort -u | wc -l)" = 1 ]
printf 'GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' |
    $B/rawsend $PORT >$R.b
check "pipelined requests both answered" [ "$(grep -c '^HTTP/1.1 200' $R.b)" = 2 ]
check "pipelined order" sh -c "grep '^path=' $R.b | tr '\n' ' ' | grep -q 'path=/a path=/b'"
printf 'GET /a HTTP/1.0\r\n\r\n' | $B/rawsend $PORT >$R.b
check "HTTP/1.0 closes" has $R.b "Connection: close"

# request bodies
code=$(curl -s -o $R.b -D $R.h -w '%{http_code}' --data-binary 'x=1&y=2' "$U/body")
check "cgi Status passthrough" [ "$code" = 201 ]
check "cgi body echo" [ "$(cat $R.b)" = 'x=1&y=2' ]
check "cgi extra header" has $R.h "X-Test: yes"
code=$(printf 'chunked body' | curl -s -o $R.b -w '%{http_code}' -H 'Transfer-Encoding: chunked' \
    --data-binary @- "$U/body")
check "chunked body" [ "$code" = 201 -a "$(cat $R.b)" = 'chunked body' ]
head -c 2000000 /dev/zero >"$TMP/big"
code=$(curl -s -o $R.b -w '%{http_code}' --data-binary @"$TMP/big" "$U/body")
check "oversize body -> 413" [ "$code" = 413 ]
code=$(curl -s -o $R.b -w '%{http_code}' -H 'Expect:' --data-binary @"$TMP/big" "$U/body")
check "oversize body without Expect -> 413" [ "$code" = 413 ]
head -c 1048576 /dev/zero >"$TMP/cap"
code=$(curl -s -o $R.b -w '%{http_code}' --data-binary @"$TMP/cap" "$U/body")
check "1 MiB body accepted" [ "$code" = 201 -a "$(wc -c <$R.b)" = 1048576 ]

# static files
code=$(curl -s -o $R.b -D $R.h -w '%{http_code}' "$U/public/style.css")
check "static 200" [ "$code" = 200 ]
check "static body" [ "$(cat $R.b)" = 'body { color: navy }' ]
check "static css type" has $R.h "Content-Type: text/css"
lm=$(sed -n 's/^Last-Modified: \(.*\)\r$/\1/p' $R.h)
code=$(curl -s -o /dev/null -w '%{http_code}' -H "If-Modified-Since: $lm" "$U/public/style.css")
check "static 304" [ "$code" = 304 ]
check "static nested" [ "$(curl -s "$U/public/sub/a.txt")" = hello ]
curl -s -I -o $R.h "$U/public/sub/a.txt"
check "static HEAD" has $R.h "Content-Length: 6"
for p in "/public/nope.css" "/public/sub" "/public/sub/" "/public/" "/public/link.txt"; do
    code=$(curl -s -o $R.b -w '%{http_code}' "$U$p")
    check "static 404 $p" [ "$code" = 404 ]
done
code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$U/public/style.css")
check "static POST -> 405" [ "$code" = 405 ]

# path traversal
for p in "/public/../.gross_secret" "/public/%2e%2e/.gross_secret" "/public/..%2f.gross_secret" \
    "/public/sub/../../.gross_secret" "/public/%2e%2e%2foutside%2fsecret.txt" "/public/..\\.gross_secret" \
    "/public/%00../.gross_secret"; do
    code=$(curl -s --path-as-is -o $R.b -w '%{http_code}' "$U$p")
    check "traversal $p refused" [ "$code" = 404 -o "$code" = 400 ]
    check "traversal $p leaks nothing" hasnt $R.b "$(cat "$G/.gross_secret")"
    check "traversal $p leaks nothing (2)" hasnt $R.b "TOP SECRET"
done

# timeouts and broken programs
t0=$(date +%s)
code=$(curl -s -o $R.b -w '%{http_code}' "$U/sleep")
t1=$(date +%s)
check "slow program -> 504" [ "$code" = 504 ]
check "504 within the limit" [ $((t1 - t0)) -le 4 ]
check "504 in voice" has $R.b "ICL504I"
code=$(curl -s -o $R.b -w '%{http_code}' "$U/garbage")
check "garbage CGI output -> 502" [ "$code" = 502 ]
printf 'GET /a HTTP/1.1\r\nHost: x\r\n' | $B/rawsend $PORT keep >$R.b
check "slow request head -> 408" has $R.b "HTTP/1.1 408"

# malformed requests
raw() { printf "$1" | $B/rawsend $PORT >$R.b; }
raw 'GARBAGE\r\n\r\n'
check "garbage request line -> 400" has $R.b "HTTP/1.1 400"
check "400 in voice" has $R.b "ICL017I"
raw 'GET\r\n\r\n'
check "method only -> 400" has $R.b "HTTP/1.1 400"
raw 'GET nopath HTTP/1.1\r\nHost: x\r\n\r\n'
check "relative target -> 400" has $R.b "HTTP/1.1 400"
raw 'GET / HTTP/2.0\r\nHost: x\r\n\r\n'
check "HTTP/2.0 request line -> 505" has $R.b "HTTP/1.1 505"
raw 'GET / HTTP/1.1\r\n\r\n'
check "missing Host -> 400" has $R.b "HTTP/1.1 400"
raw 'GET / HTTP/1.1\r\nHost: x\r\nbad header\r\n\r\n'
check "header without colon -> 400" has $R.b "HTTP/1.1 400"
raw 'POST /body HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello!'
check "conflicting Content-Length -> 400" has $R.b "HTTP/1.1 400"
raw 'POST /body HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\n'
check "negative Content-Length -> 400" has $R.b "HTTP/1.1 400"
raw 'POST /body HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nContent-Length: 3\r\n\r\n0\r\n\r\n'
check "TE + CL -> 400" has $R.b "HTTP/1.1 400"
raw 'GET /%%zz HTTP/1.1\r\nHost: x\r\n\r\n'
check "bad percent escape -> 400" has $R.b "HTTP/1.1 400"
head -c 20000 /dev/zero | tr '\0' 'a' >"$TMP/long"
{ printf 'GET / HTTP/1.1\r\nHost: x\r\nX-Long: '; cat "$TMP/long"; printf '\r\n\r\n'; } |
    $B/rawsend $PORT >$R.b
check "huge header -> 431" has $R.b "HTTP/1.1 431"
head -c 3000 /dev/urandom | $B/rawsend $PORT >$R.b
check "binary junk answered or closed" true
code=$(curl -s -o /dev/null -w '%{http_code}' -X 'BAD METHOD' "$U/")
check "space in method -> 400" [ "$code" = 400 ]
code=$(curl -s -o $R.b -w '%{http_code}' "$U/still-alive")
check "server survives malformed input" [ "$code" = 200 ]
check "malformed requests logged" grep -q '" 400 ' "$G/gross.log"

# ---------------------------------------------------------------- gross hardening
# chunk sizes: strictly 1-8 hex digits on their own line, overflow-safe cap
chunked() { raw "POST /body HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n$1"; }
chunked '1\r\nA\r\n-1\r\nBBBBBBBB\r\n0\r\n\r\n'
check "negative chunk size -> 400" has $R.b "HTTP/1.1 400"
for bad in '+0x3' '0x3' ' 3' '3 ' '' '000000003' '3\r\nabc\n0'; do
    chunked "$bad\r\nabc\r\n0\r\n\r\n"
    check "chunk size [$bad] -> 400" has $R.b "HTTP/1.1 400"
done
chunked '3\nabc\r\n0\r\n\r\n'
check "bare-LF chunk line -> 400" has $R.b "HTTP/1.1 400"
chunked 'FFFFFFFF\r\nx\r\n'
check "4 GiB chunk -> 413" has $R.b "HTTP/1.1 413"
chunked '1\r\nA\r\n100000\r\n'
check "chunks past 1 MiB -> 413" has $R.b "HTTP/1.1 413"
chunked '3;name=val\r\nabc\r\n2\r\nde\r\n0\r\n\r\n'
check "chunk extensions accepted" has $R.b "HTTP/1.1 201"
check "chunked body reassembled" has $R.b "abcde"
code=$(curl -s -o /dev/null -w '%{http_code}' "$U/still-alive")
check "server survives hostile chunks" [ "$code" = 200 ]

# "/public" itself (once read past its end)
for p in /public "/public?x"; do
    code=$(curl -s -o /dev/null -w '%{http_code}' "$U$p")
    check "$p -> 404" [ "$code" = 404 ]
done

# one address cannot take every slot: 16 idle connections, the 17th is refused
HOLD=""
i=0
while [ $i -lt 16 ]; do
    : | $B/rawsend $PORT keep >/dev/null &
    HOLD="$HOLD $!"
    i=$((i + 1))
done
sleep 0.5
code=$(curl -s -o $R.b -D $R.h -w '%{http_code}' "$U/x")
check "17th connection from one address -> 429" [ "$code" = 429 ]
check "429 says when to retry" has $R.h "Retry-After: 1"
check "429 in voice" has $R.b "ICL429I"
t0=$(date +%s)
for p in $HOLD; do wait $p; done
t1=$(date +%s)
check "idle connections dropped by the head timeout" [ $((t1 - t0)) -le 4 ]
code=$(curl -s -o /dev/null -w '%{http_code}' "$U/x")
check "slots come back" [ "$code" = 200 ]

# connection lifetime (-l 3): no new request on a connection older than 3 s
{ printf 'GET /l1 HTTP/1.1\r\nHost: x\r\n\r\n'; sleep 1; printf 'GET /l2 HTTP/1.1\r\nHost: x\r\n\r\n'
  sleep 3; printf 'GET /l3 HTTP/1.1\r\nHost: x\r\n\r\n'; } 2>/dev/null | $B/rawsend $PORT keep >$R.b
check "requests within the lifetime served" [ "$(grep -c '^path=/l[12]' $R.b)" = 2 ]
check "request after the lifetime refused" hasnt $R.b "path=/l3"

# IOI_HOSTS: other Host headers are misdirected
GROSS_ARGS="-t 2"
H="$TMP/h"
mkdir -p "$H/public"
start_gross "$H" "$ROOT/tests/web/cgi.sh" IOI_HOSTS="127.0.0.1, example.test:8080"
code=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/x")
check "listed host answered" [ "$code" = 200 ]
code=$(curl -s -o /dev/null -w '%{http_code}' -H 'Host: example.test:8080' "http://127.0.0.1:$PORT/x")
check "listed host:port answered" [ "$code" = 200 ]
code=$(curl -s -o $R.b -w '%{http_code}' -H 'Host: evil.example' "http://127.0.0.1:$PORT/x")
check "unlisted host -> 421" [ "$code" = 421 ]
check "421 in voice" has $R.b "ICL421I"
code=$(curl -s -o /dev/null -w '%{http_code}' -H 'Host: example.test:9090' "http://127.0.0.1:$PORT/x")
check "listed host on another port -> 421" [ "$code" = 421 ]

# ---------------------------------------------------------------- notes app
A="$TMP/a"
mkdir -p "$A/public"
GROSS_ARGS="-t 10"
start_gross "$A" "$ROOT/$B/notes" IOI_APP_DIR="$ROOT/tests/web/app" IOI_HOSTS=127.0.0.1
U="http://127.0.0.1:$PORT"
O="Origin: http://127.0.0.1:$PORT"

# the request head has 5 s, however long -t (10 here) allows the program
t0=$(date +%s)
printf 'GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n' | $B/rawsend $PORT keep >$R.b
t1=$(date +%s)
check "slow head -> 408 under -t 10" has $R.b "HTTP/1.1 408"
check "head timeout is 5 s, not -t ($((t1 - t0)) s)" [ $((t1 - t0)) -ge 4 -a $((t1 - t0)) -le 7 ]
J="$TMP/jar"

code=$(curl -s -o $R.b -D $R.h -w '%{http_code}' "$U/")
check "app index 200" [ "$code" = 200 ]
check "app first open seeded" has $R.b "1 notes"
check "app lists seed note escaped" has $R.b 'Bob was **here**'
check "app html type" has $R.h "Content-Type: text/html; charset=utf-8"
check "app CSP header" has $R.h "Content-Security-Policy: default-src 'self'"
check "app ago" has $R.b " ago</small>"
check "seed password hashed" [ "$(sqlite3 "$A/cassette.db" "SELECT count(*) FROM users WHERE password LIKE 'pbkdf2-sha256\$%'")" = 2 ]
check "database in WAL mode" [ "$(sqlite3 "$A/cassette.db" 'PRAGMA journal_mode')" = wal ]

curl -s -I -o $R.h "$U/"
getlen=$(curl -s "$U/" | wc -c)
check "HEAD declares the GET length" has $R.h "Content-Length: $getlen"
code=$(curl -s -o $R.b -w '%{http_code}' -d 'name=ada&password=correct+horse' "$U/login")
check "POST without Origin -> 403" [ "$code" = 403 ]
check "403 in voice" has $R.b "YOU CAN&#39;T GET THERE FROM HERE"
code=$(curl -s -o $R.b -w '%{http_code}' -H 'Origin: http://evil.example' \
    -d 'name=ada&password=correct+horse' "$U/login")
check "POST from another origin -> 403" [ "$code" = 403 ]
code=$(curl -s -o $R.b -w '%{http_code}' -H "$O" -d 'name=ada&password=wrong' "$U/login")
check "wrong password -> 401" [ "$code" = 401 ]
code=$(curl -s -o $R.b -D $R.h -c $J -w '%{http_code}' -H "$O" -d 'name=ADA&password=correct+horse' "$U/login")
check "login -> 303" [ "$code" = 303 ]
check "login redirects home" has $R.h "Location: /"
check "session cookie flags" grep -Eq '^Set-Cookie: ioi_session=[0-9a-f]{32}\.[0-9a-f]{64}; Path=/; HttpOnly; SameSite=Lax; Max-Age=2592000' $R.h
check "session stored server-side" [ "$(sqlite3 "$A/cassette.db" 'SELECT count(*) FROM ioi_sessions')" = 1 ]
curl -s -b $J -o $R.b "$U/"
check "session read back" has $R.b 'signed in as ada'
check "face rendered" has $R.b '<svg class="ick-face"'
check "face has title" has $R.b '<title>ada</title>'

forged=$(sed -n 's/.*ioi_session\t//p' $J | sed 's/^[0-9a-e]/f/;t;s/^f/0/')
curl -s -H "Cookie: ioi_session=$forged" -o $R.b "$U/"
check "forged session rejected" hasnt $R.b 'signed in as'

code=$(curl -s -o $R.b -D $R.h -b $J -w '%{http_code}' -H "Referer: http://127.0.0.1:$PORT/" \
    --data-urlencode 'body=**hi** <script>alert(1)</script> [x](javascript:alert(1)) @bob' "$U/notes")
check "create via Referer -> 303" [ "$code" = 303 ]
check "create redirects to note" has $R.h "Location: /notes/2"
code=$(curl -s -o $R.b -w '%{http_code}' "$U/notes/2")
check "note page 200" [ "$code" = 200 ]
check "note prose bold" has $R.b '<strong>hi</strong>'
check "note prose mention" has $R.b '<a class="mention" href="/u/bob">@bob</a>'
check "note prose inert script" hasnt $R.b '<script>'
check "note prose inert link" hasnt $R.b 'href="javascript'
code=$(curl -s -o $R.b -w '%{http_code}' -b $J -H "$O" --data-urlencode 'body=mine now' "$U/notes/1/edit")
check "editing someone else's note -> 403" [ "$code" = 403 ]
code=$(curl -s -o $R.b -w '%{http_code}' -b $J -H "$O" --data-urlencode 'body=*edited*' "$U/notes/2/edit")
check "edit own note -> 303" [ "$code" = 303 ]
check "edit stored" has /dev/stdin "<em>edited</em>" <<EOF
$(curl -s "$U/notes/2")
EOF
code=$(curl -s -o $R.b -w '%{http_code}' -b $J -H "$O" -X POST "$U/notes/2/delete")
check "delete own note -> 303" [ "$code" = 303 ]
code=$(curl -s -o $R.b -w '%{http_code}' "$U/notes/2")
check "deleted note -> 404" [ "$code" = 404 ]
check "404 in voice" has $R.b "ICL404I NO SUCH NOTE"
cp $J "$J.before-logout"
code=$(curl -s -o $R.b -D $R.h -b $J -c $J -w '%{http_code}' -H "$O" -X POST "$U/logout")
check "logout -> 303" [ "$code" = 303 ]
check "logout clears cookie" has $R.h "Max-Age=0"
curl -s -b $J -o $R.b "$U/"
check "logged out" hasnt $R.b 'signed in as'
code=$(curl -s -o $R.b -w '%{http_code}' -b "$J.before-logout" -H "$O" --data-urlencode 'body=zombie' "$U/notes")
check "cookie replayed after logout is dead" [ "$code" = 403 ]
check "logout deleted the session row" [ "$(sqlite3 "$A/cassette.db" 'SELECT count(*) FROM ioi_sessions')" = 0 ]

# DNS rebinding: a foreign Host with a matching Origin is refused by IOI_HOSTS
code=$(curl -s -o /dev/null -w '%{http_code}' -H 'Host: evil.example' -H 'Origin: http://evil.example' \
    -d 'name=ada&password=correct+horse' "$U/login")
check "rebinding login -> 421" [ "$code" = 421 ]

# Secure cookies (TLS terminator in front): __Host- prefix and Secure
printf 'name=ada&password=correct+horse' |
    env -i REQUEST_METHOD=POST PATH_INFO=/login HTTP_HOST=h HTTP_ORIGIN=http://h \
        CONTENT_TYPE=application/x-www-form-urlencoded CONTENT_LENGTH=31 IOI_SECRET=s \
        IOI_SECURE_COOKIES=1 IOI_APP_DIR="$ROOT/tests/web/app" IOI_DATABASE="$A/cassette.db" \
        $B/notes >$R.b
check "secure cookie name" has $R.b "Set-Cookie: __Host-ioi_session="
check "secure cookie flag" grep -q '^Set-Cookie: __Host-ioi_session=.*; Secure' $R.b

# a missing account costs as much PBKDF2 time as a wrong password
login_ms() {
    for i in 1 2 3 4 5; do
        curl -s -o /dev/null -w '%{time_total}\n' -H "$O" -d "$1" "$U/login"
    done | awk '{ s += $1 } END { printf "%d\n", s * 1000 / NR }'
}
known=$(login_ms 'name=ada&password=wrong-password')
unknown=$(login_ms 'name=nobody-here&password=wrong-password')
ok_ms=$(login_ms 'name=ada&password=correct+horse')
echo "sign-in timing: success ${ok_ms} ms, wrong password ${known} ms, unknown user ${unknown} ms"
check "unknown user is not faster than a wrong password (${unknown} vs ${known} ms)" \
    [ $((unknown * 10)) -ge $((known * 6)) ]
check "sign-in stays fast (${ok_ms} ms)" [ "$ok_ms" -lt 200 ]

# a reset database does not honour old cookies
curl -s -o /dev/null -c "$J.reset" -H "$O" -d 'name=ada&password=correct+horse' "$U/login"
rm -f "$A/cassette.db" "$A/cassette.db-wal" "$A/cassette.db-shm"
curl -s -b "$J.reset" -o $R.b "$U/"
check "reset database reseeded" has $R.b "1 notes"
check "old cookie means nothing after a reset" hasnt $R.b 'signed in as'

check "digit words in route param" [ "$(curl -s "$U/number/ONE%20TWO")" = "found=1 len=7 ok=1 value=12" ]
check "digit words in query" [ "$(curl -s "$U/number?n=oh+niner")" = "found=1 len=8 ok=1 value=9" ]
check "absent field" [ "$(curl -s "$U/number")" = "found=0 len=0 ok=0 value=0" ]
code=$(curl -s -o $R.b -w '%{http_code}' "$U/boom")
check "runtime error -> 500" [ "$code" = 500 ]
check "500 page text" has $R.b "ICL774I RANDOM COMPILER BUG"
check "500 discards partial output" hasnt $R.b "partial output"
code=$(curl -s -o $R.b -w '%{http_code}' "$U/no/such/route")
check "no route -> 404" [ "$code" = 404 ]
check "app runs without gross" [ "$(env -i $B/notes)" = "not a web request" ]

# ---------------------------------------------------------------- done
echo "web: $passed passed, $failed failed"
[ $failed -eq 0 ]
