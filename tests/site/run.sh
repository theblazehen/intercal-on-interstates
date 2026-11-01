#!/bin/sh
# tests/site/run.sh — the four examples of reference/examples.html
# (examples/site/) and the kitchen-sink guestbook (tests/site/), compiled
# with build/ick -s -b and served by build/gross on random ports.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT" || exit 1
if ! command -v "${CC:-cc}" >/dev/null 2>&1; then
    exec nix develop -c sh "$ROOT/tests/site/run.sh" "$@"
fi
[ -x build/ick ] && [ -x build/gross ] && [ -f build/lib/libioi.a ] || {
    echo "run make first" >&2
    exit 1
}
TMP=$(mktemp -d)
PIDS=""
cleanup() {
    for p in $PIDS; do kill "$p" 2>/dev/null; done
    for p in $PIDS; do wait "$p" 2>/dev/null; done
    if [ -n "${KEEP:-}" ]; then echo "kept $TMP" >&2; else rm -rf "$TMP"; fi
}
trap cleanup EXIT INT TERM

passed=0
failed=0
ok() { passed=$((passed + 1)); }
bad() { failed=$((failed + 1)); echo "FAIL: $*" >&2; }
check() {
    desc=$1
    shift
    if "$@"; then ok; else bad "$desc"; fi
}
has() { grep -qF -- "$2" "$1"; }
hasnt() { ! grep -qF -- "$2" "$1"; }
is() { [ "$1" = "$2" ]; }

# start_gross DIR PROG [ENV...]: gross in DIR on a free random port -> PORT, U
start_gross() {
    dir=$1
    prog=$2
    shift 2
    tries=0
    while [ $tries -lt 20 ]; do
        tries=$((tries + 1))
        PORT=$(( ( $(od -An -N2 -tu2 /dev/urandom) % 10000 ) + 20000 ))
        (cd "$dir" && exec env "$@" "$ROOT/build/gross" -p "$PORT" -d "$dir/public" -b 127.0.0.1 \
            -t 5 "$prog") 2>>"$dir/gross.log" &
        GPID=$!
        i=0
        while [ $i -lt 50 ]; do
            if curl -s -o /dev/null "http://127.0.0.1:$PORT/public/" 2>/dev/null; then
                PIDS="$PIDS $GPID"
                U="http://127.0.0.1:$PORT"
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
stop_gross() {
    kill "$GPID" 2>/dev/null
    wait "$GPID" 2>/dev/null
}

# req NAME PATH [CURL ARGS...]: response in $R/NAME.h / NAME.b, status in CODE
R="$TMP/resp"
mkdir -p "$R"
req() {
    name=$1
    path=$2
    shift 2
    CODE=$(curl -s -o "$R/$name.b" -D "$R/$name.h" -w '%{http_code}' "$@" "$U$path")
}
loc() { tr -d '\r' <"$R/$1.h" | sed -n 's/^Location: //p'; }

# ------------------------------------------------------------- the examples
S="$TMP/site"
mkdir -p "$S"
cp -r examples/site/templates examples/site/public examples/site/schema.sql examples/site/seed.sql "$S/"
for p in hello routing database blog; do
    cp "examples/site/$p.i" "$S/"
    check "compile examples/site/$p.i" build/ick -s -b "$S/$p.i"
done
# first use creates and seeds the database (run once from a shell)
(cd "$S" && ./database >"$TMP/db.out")
check "database example from a shell" has "$TMP/db.out" "<h1>Hello, world!</h1>"

start_gross "$S" ./hello
req h1 /
check "hello: 200" is "$CODE" 200
check "hello: page" has $R/h1.b "<h1>Hello, world!</h1>"
check "hello: html" has $R/h1.h "Content-Type: text/html; charset=utf-8"
req h2 /missing
check "hello: no route is 404" is "$CODE" 404
check "hello: 404 in voice" has $R/h2.b "ICL404I"
req h3 / -I
check "hello: HEAD" is "$CODE" 200
req h4 /public/site.css
check "hello: public files" has $R/h4.b "ick-bar"
stop_gross

start_gross "$S" ./routing
req r1 /home
check "routing: /home 200" is "$CODE" 200
check "routing: /home page" has $R/r1.b "Welcome to the home page"
check "routing: /home links the blog" has $R/r1.b 'href="/blog/post/1"'
req r2 /blog/post/1
check "routing: post without Referer is 403" is "$CODE" 403
check "routing: 403 in voice" has $R/r2.b "YOU CAN&#39;T GET THERE FROM HERE"
req r3 /blog/post/1 -H "Referer: $U/home"
check "routing: post from /home 200" is "$CODE" 200
check "routing: blog_post.html" has $R/r3.b "<h1></h1> <!-- Title -->"
check "routing: blog_post.html date" has $R/r3.b "<h3>Posted </h3>"
req r4 /
check "routing: / is not routed" is "$CODE" 404
stop_gross

start_gross "$S" ./database
req d1 /
check "database: 200" is "$CODE" 200
check "database: column 3 of row 1" has $R/d1.b "<h1>Hello, world!</h1>"
stop_gross

start_gross "$S" ./blog
req b1 /blog/post/1
check "blog: 200" is "$CODE" 200
check "blog: title" has $R/b1.b "<h1>INTERCAL comes to the Interstates</h1>"
check "blog: date" has $R/b1.b "<h3>Posted 2013-04-01</h3>"
check "blog: escaped body" has $R/b1.b "future &amp; the past"
req b2 /blog/post/2
check "blog: second post" has $R/b2.b "<h1>COME FROM considered helpful</h1>"
check "blog: html escaped" has $R/b2.b "&lt;Abstain&gt;"
req b3 /blog/post/99
check "blog: missing post is a runtime error" is "$CODE" 500
check "blog: error text" has $R/b3.b "ICL241I"
stop_gross

# ------------------------------------------------------------- kitchen sink
K="$TMP/ks"
mkdir -p "$K"
cp -r tests/site/templates tests/site/public tests/site/schema.sql tests/site/seed.sql \
    tests/site/kitchen_sink.i "$K/"
check "compile kitchen_sink.i" build/ick -s -b "$K/kitchen_sink.i"
(cd "$K" && ./kitchen_sink >"$TMP/ks.out" 2>"$TMP/ks.err")
check "kitchen sink: shell run exits 0" is "$?" 0
cat >"$TMP/ks.want" <<'EOF'
      
CXXIII
__
IV
   
LXV
    
LXVI
      
CXXIII
__
IV
  
II
  
II
   
XII
  
XI
   
III
shell mode: 123 CXXIII four four 4000 65 done
EOF
check "kitchen sink: shell output" cmp -s "$TMP/ks.out" "$TMP/ks.want"

start_gross "$K" ./kitchen_sink IOI_NOW=1365033600
O="Origin: $U"
req k1 /
check "ks /: 200" is "$CODE" 200
check "ks /: count" has $R/k1.b '<p class="count">3 entries (III), 3 likes</p>'
check "ks /: anonymous" has $R/k1.b '<a href="/login">Sign in</a>'
check "ks /: newest first" has $R/k1.b '<li id="e3" data-index="1">'
check "ks /: loop index" has $R/k1.b '<a href="/entry/1">#III</a>'
check "ks /: prose" has $R/k1.b '<strong>analytical engine</strong>'
check "ks /: xss inert" has $R/k1.b '&lt;script&gt;alert(1)&lt;/script&gt;'
check "ks /: face" has $R/k1.b 'class="ick-face"'
check "ks /: ago" has $R/k1.b '1 day ago'
check "ks /: IS hides zero likes" hasnt $R/k1.b '0 PLEASEs'
check "ks /: likes shown" has $R/k1.b '2 PLEASEs'
check "ks /: loop restores .144" has $R/k1.b 'back to 0'
req k2 /entry/1
check "ks entry: 200" is "$CODE" 200
check "ks entry: heading" has $R/k2.b '<h1>Entry 1 by <a href="/u/ada">ada</a></h1>'
check "ks entry: roman likes" has $R/k2.b '<p class="likes">II</p>'
check "ks entry: AS IS" has $R/k2.b '<p class="raw">First! The **analytical engine** approves.</p>'
req k3 /entry/99
check "ks missing entry: 404" is "$CODE" 404
check "ks missing entry: page" has $R/k3.b "E404 NO SUCH ENTRY"
req k4 /sign -X POST --data-urlencode "message=Hello *there*"
check "ks sign without Origin: 403" is "$CODE" 403
req k5 /sign -X POST -H "$O" --data-urlencode "message=Hello *there*"
check "ks sign: 303" is "$CODE" 303
check "ks sign: Location" is "$(loc k5)" /entry/4
req k6 /entry/4
check "ks new entry" has $R/k6.b '<p>Hello <em>there</em></p>'
check "ks new entry: anonymous author" has $R/k6.b 'by <a href="/u/anonymous">anonymous</a>'
req k7 /sign -X POST -H "$O" --data-urlencode "message="
check "ks empty message: 400" is "$CODE" 400
check "ks empty message: text" has $R/k7.b "YOUR MESSAGE IS EMPTY"
req k8 /like/1 -X POST -H "$O"
check "ks like: 303" is "$CODE" 303
req k9 /entry/1
check "ks like: three likes" has $R/k9.b '<p class="likes">III</p>'
req k10 /login
check "ks login form" has $R/k10.b 'action="/login"'
req k11 /login -X POST -H "$O" --data "name=ada&password=wrong"
check "ks bad password: 403" is "$CODE" 403
check "ks bad password: text" has $R/k11.b "PASSWORD REJECTED"
req k12 /login -X POST -H "$O" --data "name=ada&password=lovelace1815"
check "ks login: 303" is "$CODE" 303
COOKIE=$(tr -d '\r' <$R/k12.h | sed -n 's/^Set-Cookie: \([^;]*\).*/\1/p' | head -n 1)
check "ks login: cookie" [ -n "$COOKIE" ]
req k13 / -H "Cookie: $COOKIE"
check "ks signed in" has $R/k13.b '<p class="user">Signed in as ada</p>'
req k14 /me -H "Cookie: $COOKIE"
check "ks /me redirects" is "$(loc k14)" /u/ada
req k15 /me
check "ks /me anonymous: 403" is "$CODE" 403
req k16 /sign -X POST -H "$O" -H "Cookie: $COOKIE" --data "message=signed"
req k17 "$(loc k16)"
check "ks signed entry by ada" has $R/k17.b 'by <a href="/u/ada">ada</a>'
req k18 /logout -X POST -H "$O" -H "Cookie: $COOKIE"
check "ks logout: 303" is "$CODE" 303
check "ks logout clears cookie" has $R/k18.h "Set-Cookie:"
req k19 /u/ada
check "ks user page" has $R/k19.b '<p class="posts">III entries</p>'
check "ks user entries" has $R/k19.b 'href="/entry/5"'
req k20 /u/nobody
check "ks unknown user: 404" is "$CODE" 404
req k21 "/search?q=HELLO"
check "ks search: hits" has $R/k21.b '2 hits, 2 liked entries overall'
check "ks search: result" has $R/k21.b 'Hello from the `COME FROM` lane'
req k22 "/search?q=hello&skip=1"
check "ks search: skip" hasnt $R/k22.b 'href="/entry/3"'
req k23 /files/a/b/c.txt
check "ks splat: text/plain" has $R/k23.h "Content-Type: text/plain"
check "ks splat: param" has $R/k23.b "You asked for a/b/c.txt via a splat"
req k24 /time
check "ks time" has $R/k24.b "1365033600 just now seconds since 1970"
req k25 /api/stats
check "ks stats: json" has $R/k25.h "Content-Type: application/json"
check "ks stats: body" has $R/k25.b '{"entries": 5, "likes": 4, "first": 1}'
req k26 /api/entries
check "ks entries json" has $R/k26.b '[{"id": 1, "likes": 3}, {"id": 2, "likes": 0}'
req k27 /page/2
check "ks page 2" has $R/k27.b 'href="/entry/3"'
check "ks page 2 skips" hasnt $R/k27.b 'href="/entry/1"'
check "ks page 2 next" has $R/k27.b 'href="/page/3"'
req k28 "/echo?n=ONE+TWO&text=caf%C3%A9"
check "ks echo" has $R/k28.b "n=XII (12) text=café bytes=5 absent=0 last=0"
req k29 /entry/4/redact -X POST -H "Referer: $U/entry/4"
check "ks redact: 303" is "$CODE" 303
req k30 /entry/4
check "ks redact: message" has $R/k30.b "[redacted by a moderator]"
req k31 /entry/4/delete -X POST
check "ks delete without Referer: 403" is "$CODE" 403
req k32 /entry/4/delete -X POST -H "Referer: $U/entry/4"
check "ks delete: 303 to /" is "$(loc k32)" /
req k33 /entry/4
check "ks deleted entry: 404" is "$CODE" 404
req k34 /no/such/page
check "ks catch-all: 404" is "$CODE" 404
check "ks catch-all: own page" has $R/k34.b "E404 NO SUCH ENTRY"
req k35 / -I
check "ks HEAD" is "$CODE" 200
stop_gross

# ------------------------------------- Roman numerals in each HTML context
# Overbar spans only in element content; attributes, comments and raw-text
# elements get U+0305 COMBINING OVERLINE instead (no markup can escape).
RM="$TMP/roman"
mkdir -p "$RM/templates"
cat >"$RM/templates/r.html" <<'EOF'
<title>{{#4000}}</title>
<a title="{{#4000}}" data-x='{{.1 IN ROMAN}}' href=/public/{{#5000}}>{{#4000}} {{,1 IN ROMAN}}</a>
<!-- {{#4000}} --><textarea>{{#9000}}</textarea><p>{{#9000}}</p>
<script>var x = "{{#4000}}";</script><b>{{#3}}</b>
EOF
printf 'DO COME FROM (16000) WITH ROUTE "/r"\nDO .1 <- #4000\nPLEASE DO ,1 <- "6000"\nDO READ OUT #0 WITH TEMPLATE "r.html"\nDO GIVE UP\n' >"$RM/r.i"
check "roman contexts: compile" build/ick -s -b "$RM/r.i"
(cd "$RM" && ./r >"$RM/out" 2>&1)
cat >"$RM/want" <<'EOF'
<title>I̅V̅</title>
<a title="I̅V̅" data-x='I̅V̅' href=/public/V̅><span class="ick-bar">IV</span> <span class="ick-bar">VI</span></a>
<!-- I̅V̅ --><textarea>I̅X̅</textarea><p><span class="ick-bar">IX</span></p>
<script>var x = "I̅V̅";</script><b>III</b>
EOF
check "roman contexts: output" cmp -s "$RM/out" "$RM/want"
start_gross "$RM" ./r
req rm1 /r
check "roman contexts: served" cmp -s "$R/rm1.b" "$RM/want"
check "roman contexts: no span in attribute" hasnt $R/rm1.b 'title="<span'
stop_gross

# ------------------------------------------------------- compile-time errors
E="$TMP/err"
mkdir -p "$E/templates"
printf '<a href="/nowhere">x</a>\n' >"$E/templates/bad.html"
printf '<p>{{,144</p>\n' >"$E/templates/grumpy.html"
printf 'DO COME FROM (16000)\nPLEASE READ OUT #0 WITH TEMPLATE "bad.html"\nDO GIVE UP\n' >"$E/e401.i"
printf 'DO COME FROM (5) WITH ROUTE "/x"\n(5) DO GIVE UP\nPLEASE GIVE UP\n' >"$E/e400.i"
printf 'DO COME FROM (16000)\nPLEASE READ OUT #0 WITH TEMPLATE "nope.html"\nDO GIVE UP\n' >"$E/e402.i"
printf 'DO COME FROM (16000)\nPLEASE READ OUT #0 WITH TEMPLATE "grumpy.html"\nDO GIVE UP\n' >"$E/e406.i"
for c in 400 401 402 406; do
    build/ick -s -b "$E/e$c.i" 2>"$E/e$c.err"
    check "E$c exit status" is "$?" "$((c % 256))"
    check "E$c message" grep -q "ICL${c}I" "$E/e$c.err"
done
check "E401 names file and line" grep -q "^$E/e401.i:2: ICL401I" "$E/e401.err"
check "E400 names file and line" grep -q "^$E/e400.i:1: ICL400I" "$E/e400.err"
check "E406 names the template" grep -q "grumpy.html:1: ICL406I" "$E/e406.err"

echo "site: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
