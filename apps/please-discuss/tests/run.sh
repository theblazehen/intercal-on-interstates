#!/bin/sh
# apps/please-discuss/tests/run.sh — end-to-end test of PLEASE DISCUSS (forum.i).
# Compiles forum.i with build/ick -s -b into a temp dir, serves it with
# build/gross on random ports and drives it with curl:
#   phase 1: an empty forum (schema only, no seed users): the first account
#            becomes the admin, the second a member;
#   phase 2: the seeded forum: accounts, topics, replies, quote, edit,
#            delete, PLEASE, moderation, roles, search, pagination,
#            validation, CSRF and XSS.
# Run from anywhere; exits non-zero on any failure. KEEP=1 keeps the temp dir.
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
APP=apps/please-discuss
cd "$ROOT" || exit 1
if ! command -v "${CC:-cc}" >/dev/null 2>&1; then
    exec nix develop -c sh "$ROOT/$APP/tests/run.sh" "$@"
fi
[ -x build/ick ] && [ -x build/gross ] && [ -f build/lib/libioi.a ] || {
    echo "run make first" >&2
    exit 1
}
TMP=$(mktemp -d)
GPID=""
cleanup() {
    [ -n "$GPID" ] && { kill "$GPID" 2>/dev/null; wait "$GPID" 2>/dev/null; }
    if [ -n "${KEEP:-}" ]; then echo "kept $TMP" >&2; else rm -rf "$TMP"; fi
}
trap cleanup EXIT INT TERM

passed=0
failed=0
check() {
    desc=$1
    shift
    if "$@"; then passed=$((passed + 1)); else failed=$((failed + 1)); echo "FAIL: $desc" >&2; fi
}
has() { grep -qF -- "$2" "$1"; }
hasnt() { ! grep -qF -- "$2" "$1"; }
is() { [ "$1" = "$2" ]; }

# start_gross APPDIR DB: serve $TMP/forum on a free random port -> U
start_gross() {
    tries=0
    while [ $tries -lt 20 ]; do
        tries=$((tries + 1))
        PORT=$(( ( $(od -An -N2 -tu2 /dev/urandom) % 10000 ) + 20000 ))
        (cd "$TMP" && exec env IOI_APP_DIR="$1" IOI_DATABASE="$2" "$ROOT/build/gross" -p "$PORT" \
            -d "$ROOT/$APP/public" -b 127.0.0.1 -t 10 ./forum) 2>>"$TMP/gross.log" &
        GPID=$!
        i=0
        while [ $i -lt 50 ]; do
            if curl -s -o /dev/null "http://127.0.0.1:$PORT/public/forum.css" 2>/dev/null; then
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
    GPID=""
}

# Requests. WHO is a cookie jar name (guest = no cookies). The response body
# is in $B, headers in $H, status in CODE, Location (path only) in LOC.
B="$TMP/body"
H="$TMP/head"
jar() { [ "$1" = guest ] || printf '%s\n' "-b $TMP/jar.$1 -c $TMP/jar.$1"; }
send() {
    j=$(jar "$1")
    shift
    # shellcheck disable=SC2086
    CODE=$(curl -s -o "$B" -D "$H" -w '%{http_code}' $j "$@")
    LOC=$(tr -d '\r' <"$H" | sed -n 's/^[Ll]ocation: *//p' | sed "s|^$U||")
}
# get WHO PATH [CURL ARGS]          plain GET (no Origin/Referer)
get() { w=$1; p=$2; shift 2; send "$w" "$@" "$U$p"; }
# from WHO PAGE PATH [CURL ARGS]    GET with a same-host Referer
from() { w=$1; r=$2; p=$3; shift 3; send "$w" -e "$U$r" "$@" "$U$p"; }
# post WHO PATH [FIELD=VALUE ...]   same-origin form POST
post() {
    w=$1; p=$2; shift 2
    n=$#
    i=0
    while [ $i -lt $n ]; do
        set -- "$@" --data-urlencode "$1"
        shift
        i=$((i + 1))
    done
    [ $# -eq 0 ] && set -- -d ""
    send "$w" -H "Origin: $U" "$@" "$U$p"
}
# post_file WHO PATH FIELD FILE [CURL ARGS]: one field read from a file
post_file() {
    w=$1; p=$2; f=$3; file=$4; shift 4
    send "$w" -H "Origin: $U" --data-urlencode "$f@$file" "$@" "$U$p"
}
views() { grep -o 'title="[0-9]*">[^<]*</span> views' "$B" | sed 's/title="\([0-9]*\)".*/\1/'; }
signin() { post "$1" /login "name=$2" "password=$3"; }
# follow WHO: follow 303s (at most four) from the last response
follow() { n=0; while [ "$CODE" = 303 ] && [ $n -lt 4 ]; do get "$1" "$LOC"; n=$((n + 1)); done; }
# noted TEXT: the page shows the PLEASE NOTE banner with TEXT
noted() { has "$B" "PLEASE NOTE:</b> $1"; }

# ------------------------------------------------------------------ build
check "forum.i compiles (static link check, politeness)" build/ick -s -b -o "$TMP/forum" "$APP/forum.i"
[ -x "$TMP/forum" ] || { echo "no binary" >&2; exit 1; }

# ===================================================== phase 1: empty forum
mkdir -p "$TMP/empty"
cp "$APP/schema.sql" "$TMP/empty/"
start_gross "$TMP/empty" "$TMP/empty.db"

get guest /
check "empty: board index 200" is "$CODE" 200
check "empty: board index renders" has "$B" "PLEASE DISCUSS"
post first /register "name=Founder" "password=correct-horse" "confirm=correct-horse"
check "empty: register 303" is "$CODE" 303
check "empty: register lands on / with a notice" is "$LOC" "/?done=1"
check "empty: register signs in" has "$H" "ioi_session="
follow first
check "notice: registered" noted "you are registered and signed in."
get first /u/founder
check "empty: first account is admin" has "$B" 'class="role role-3"'
check "empty: profile is case-insensitive" has "$B" ">Founder <span"
get first /
check "empty: nav shows the signed-in name" has "$B" '<span class="me-name">Founder</span>'
post second /register "name=Second" "password=correct-horse" "confirm=correct-horse"
check "empty: second register 303" is "$CODE" 303
get second /u/Second
check "empty: second account is a member" has "$B" 'class="role role-1"'
get first /u/Second
check "empty: admin sees the promote box" has "$B" "Promote to moderator"
stop_gross

# ================================================== phase 2: seeded forum
start_gross "$ROOT/$APP" "$TMP/seeded.db"
# first use creates and seeds the database (PBKDF2 for ten users)
get guest /

# ---- read-only pages carry real data
get guest /
check "index: 200" is "$CODE" 200
check "index: categories" has "$B" "COME FROM Considered Helpful"
check "index: sections" has "$B" "Rest Stops"
check "index: last topic" has "$B" "INTERCAL on Interstates 1.1 is out"
check "index: Roman numeral counts with arabic tooltip" has "$B" 'title="5 topics">V<'
check "index: faces" has "$B" 'class="ick-face"'
check "index: nav tab" has "$B" 'href="/" aria-current="page"'
get guest /latest
check "latest: 200" is "$CODE" 200
check "latest: category badges" has "$B" 'badge-cat'
check "latest: topics" has "$B" "Post your terminal setup"
get guest /c/2
check "category: 200" is "$CODE" 200
check "category: its topics" has "$B" "YOU CAN ONLY COME FROM A PAGE"
check "category: sign in to post" has "$B" 'href="/login?next=/c/2"'
get guest /c/99
check "category: missing is 404" is "$CODE" 404
check "category: 404 in voice" has "$B" "E404 NO SUCH CATEGORY"
check "category: error asks to resubnit" has "$B" "PLEASE CORRECT SOURCE AND RESUBNIT"
get guest /c/abc
check "category: non-numeric id is 404" is "$CODE" 404
get guest /t/6
check "topic: 200" is "$CODE" 200
check "topic: posts rendered as prose" has "$B" '<div class="prose">'
check "topic: inline code from markdown" has "$B" "<code>"
check "topic: edited marker" has "$B" 'class="edited"'
check "topic: author tombstone" has "$B" "ABSTAINED FROM by its author"
check "topic: role badge" has "$B" '>moderator</span>'
check "topic: guests get no PLEASE button" hasnt "$B" 'action="/p/17/please"'
check "topic: guests get the sign-in prompt" has "$B" "Want to join in?"
check "topic: permalink anchors" has "$B" 'id="p17"'
v1=$(views)
get guest /t/6
v2=$(views)
check "topic: view counter counts ($v1 -> $v2)" is "$v2" "$((v1 + 1))"
get guest /t/13
check "topic: moderator tombstone" has "$B" "ABSTAINED FROM by a moderator"
get guest /t/99
check "topic: missing is 404" is "$CODE" 404
check "topic: 404 in voice" has "$B" "E404 NO SUCH TOPIC"
get guest /p/17
check "permalink: 303" is "$CODE" 303
check "permalink: page and anchor" is "$LOC" "/t/6?page=1#p17"
get guest /p/99999
check "permalink: missing is 404" has "$B" "E404 NO SUCH POST"
get guest /u/spark_ears
check "profile: 200" is "$CODE" 200
check "profile: role" has "$B" 'class="role role-2"'
check "profile: recent posts link permalinks" has "$B" 'href="/p/'
get guest /u/nobody_here
check "profile: missing is 404" has "$B" "E404 NO SUCH USER"
get guest /nowhere/at/all
check "catch-all: 404" is "$CODE" 404
check "catch-all: in voice" has "$B" "E404 NO SUCH PAGE"

# ---- pagination boundaries (topic 12 has 23 posts: two pages)
get guest "/t/12"
check "pages: topic page 1" has "$B" 'Page <b title="1">I</b> of <span title="2">II</span>'
check "pages: next link" has "$B" 'href="/t/12?page=2"'
get guest "/t/12?page=0"
check "pages: page 0 is page 1" has "$B" 'Page <b title="1">I</b>'
get guest "/t/12?page=abc"
check "pages: non-numeric page is page 1 (200)" is "$CODE" 200
check "pages: non-numeric page shows page 1" has "$B" 'Page <b title="1">I</b>'
get guest "/t/12?page=2"
check "pages: last page 200" is "$CODE" 200
check "pages: previous link" has "$B" 'href="/t/12?page=1"'
check "pages: no next on the last page" hasnt "$B" 'href="/t/12?page=3"'
get guest "/t/12?page=3"
check "pages: past the last page is 404" is "$CODE" 404
check "pages: NO SUCH PAGE" has "$B" "E404 NO SUCH PAGE"
get guest "/t/12?page=99999999999"
check "pages: absurd page is 404 or page 1, never 500" is "$(echo "$CODE" | grep -c '^[24]0[04]$')" 1
get guest "/latest?page=2"
check "pages: latest has one page" is "$CODE" 404
get guest "/c/1?page=2"
check "pages: category past the end is 404" is "$CODE" 404
get guest "/t/6"
check "pages: a one-page topic has no pager" hasnt "$B" 'class="pager'

# ---- search
get guest "/search?q=lagoon"
check "search: 200" is "$CODE" 200
check "search: finds the post by body" has "$B" 'href="/p/39"'
get guest "/search?q=terminal+setup"
check "search: finds the topic by title" has "$B" "Post your terminal setup"
get guest "/search?q=zzqqxxnothing"
check "search: no results is 200" is "$CODE" 200
check "search: nothing found" hasnt "$B" 'href="/p/'
get guest "/search"
check "search: empty query page" is "$CODE" 200
get guest "/search?q=the&page=99"
check "search: past the last page is 404" is "$CODE" 404

# ---- CSRF: no Origin, no Referer
send guest -d "name=admin&password=please-give-up" "$U/login"
check "csrf: POST /login without Origin is 403" is "$CODE" 403
check "csrf: in voice" has "$B" "YOU CAN&#39;T GET THERE FROM HERE"
send guest -H "Origin: http://evil.example" -d "name=admin" "$U/login"
check "csrf: POST with a foreign Origin is 403" is "$CODE" 403
get guest /t/6/reply
check "csrf: origin-130 GET without Referer is 403" is "$CODE" 403

# ---- registration validation
reg() { post guest /register "name=$1" "password=$2" "confirm=$3"; }
reg "" "long-enough" "long-enough"
check "register: empty name 200" is "$CODE" 200
check "register: empty name message" has "$B" "E422 USERNAMES ARE 3 TO 20 LETTERS, DIGITS, _ OR -"
reg "ab" "long-enough" "long-enough"
check "register: 2-byte name" has "$B" "E422 USERNAMES ARE 3 TO 20"
reg "abcdefghijklmnopqrstu" "long-enough" "long-enough"
check "register: 21-byte name" has "$B" "E422 USERNAMES ARE 3 TO 20"
reg "bad name" "long-enough" "long-enough"
check "register: space in name" has "$B" "E422 USERNAMES ARE 3 TO 20"
reg "<b>x</b>" "long-enough" "long-enough"
check "register: markup in name rejected" has "$B" "E422 USERNAMES ARE 3 TO 20"
check "register: rejected name re-rendered inert" has "$B" "&lt;b&gt;x&lt;/b&gt;"
check "register: no raw markup" hasnt "$B" "<b>x</b>"
reg "SPARK_EARS" "long-enough" "long-enough"
check "register: duplicate name, any case" has "$B" "E422 THAT USERNAME IS TAKEN"
check "register: name kept" has "$B" 'value="SPARK_EARS"'
reg "tester" "short" "short"
check "register: short password" has "$B" "E422 PASSWORDS NEED AT LEAST 8 BYTES"
reg "tester" "long-enough" "long-enougH"
check "register: mismatched confirmation" has "$B" "E422 THE PASSWORDS DO NOT MATCH"
reg "tester" "long-enough" "long-enough!"
check "register: confirmation of another length" has "$B" "E422 THE PASSWORDS DO NOT MATCH"
check "register: passwords are not echoed" hasnt "$B" "long-enough"

# ---- register, sign out, sign in
post tester /register "name=tester" "password=long-enough" "confirm=long-enough"
check "register: 303" is "$CODE" 303
get tester /u/tester
check "register: seeded forum makes members" has "$B" 'class="role role-1"'
get tester /login
check "login: signed in users are sent home" is "$CODE" 303
get tester "/login?next=/c/2"
check "login: signed in users go straight to the return path" is "$LOC" /c/2
post tester /logout
check "logout: 303 to / with a notice" is "$LOC" "/?done=3"
check "logout: clears the cookie" has "$H" "Max-Age=0"
follow tester
check "notice: signed out" noted "you have GIVEN UP. You are signed out."
get tester /
check "logout: signed out" has "$B" 'href="/login?next=/"'
check "notice: gone after navigating" hasnt "$B" 'class="flash"'
signin tester tester wrong-password
check "login: wrong password 200" is "$CODE" 200
check "login: wrong password message" has "$B" "E401 THAT NAME AND PASSWORD DO NOT MINGLE"
check "login: name kept" has "$B" 'value="tester"'
signin tester TESTER long-enough
check "login: 303 to / with a notice" is "$LOC" "/?done=2"
follow tester
check "notice: signed in" noted "you are signed in."
get tester /
check "login: signed in" has "$B" '<span class="me-name">tester</span>'
signin mod spark_ears rabbit-ears
signin admin admin please-give-up
signin other come_from computed-come-from

# ---- the return path (next): carried by links, validated on sign-in
get guest "/t/12?page=2"
check "next: nav Sign in carries the page" has "$B" 'href="/login?next=/t/12?page=2"'
check "next: nav Register carries it too" has "$B" 'href="/register?next=/t/12?page=2"'
check "next: the reply prompt carries it" has "$B" 'Want to join in?</span> <a href="/login?next=/t/12?page=2"'
get guest /c/2
check "next: Sign in to post carries the category" has "$B" 'href="/login?next=/c/2"'
get guest /u/spark_ears
check "next: profiles" has "$B" 'href="/login?next=/u/spark_ears"'
get guest "/login?next=/t/6"
check "next: kept in the sign-in form" has "$B" 'name="next" value="/t/6"'
check "next: carried on to Register" has "$B" 'href="/register?next=/t/6"'
get guest "/register?next=/t/6"
check "next: kept in the register form" has "$B" 'name="next" value="/t/6"'
check "next: carried back to Sign in" has "$B" 'href="/login?next=/t/6"'
get guest "/login?next=//evil.example"
check "next: //host dropped from the form" has "$B" 'name="next" value=""'
nx() { post nx /login "name=newbie" "password=hello-world" "next=$1"; }
nx /t/6
check "next: sign-in returns to the page" is "$LOC" "/t/6?done=2"
follow nx
check "next: lands there" has "$B" "<title>ActiveCassette"
check "next: with the signed-in notice" noted "you are signed in."
nx "/t/12?page=2"
check "next: a query gets &done" is "$LOC" "/t/12?page=2&done=2"
nx "/new?c=3"
check "next: /new?c=3" is "$LOC" "/new?c=3&done=2"
nx "//evil.example"
check "next: //evil.example rejected" is "$LOC" "/?done=2"
nx "https://evil.example"
check "next: https://evil.example rejected" is "$LOC" "/?done=2"
nx '/\evil.example'
check "next: /\\evil.example rejected" is "$LOC" "/?done=2"
nx "evil.example/t/1"
check "next: no leading slash rejected" is "$LOC" "/?done=2"
nx "/a b"
check "next: spaces rejected" is "$LOC" "/?done=2"
nx "/t/6#p17"
check "next: fragments rejected" is "$LOC" "/?done=2"
nx ""
check "next: empty means /" is "$LOC" "/?done=2"
send nx -H "Origin: $U" --data "name=newbie&password=hello-world&next=/t/6%0D%0ASet-Cookie:%20x=1" "$U/login"
check "next: CR/LF rejected" is "$LOC" "/?done=2"
check "next: no header injection" hasnt "$H" "x=1"
get nx "/login?next=/c/2"
check "next: signed in, GET /login goes there" is "$LOC" /c/2
get guest "/new?c=2"
check "next: /new bounces with its return path" is "$LOC" "/login?next=/new?c=2"
post bounce /login "name=newbie" "password=hello-world" "next=${LOC#/login?next=}"
check "next: and comes back after sign-in" is "$LOC" "/new?c=2&done=2"
follow bounce
check "next: to the editor, category kept" has "$B" '<option value="2" selected>'
post returner /register "name=returner" "password=long-enough" "confirm=long-enough" "next=/c/4"
check "next: register returns too" is "$LOC" "/c/4?done=1"
follow returner
check "notice: registered, on the return page" noted "you are registered and signed in."
get guest "/?done=99"
check "notice: codes outside the table show nothing" hasnt "$B" 'class="flash"'
get guest "/?done=65538"
check "notice: high bits show nothing" hasnt "$B" 'class="flash"'
get guest "/?done=%3Cscript%3E"
check "notice: text is never echoed" hasnt "$B" 'class="flash"'
get guest "/?done=99999999999"
check "notice: absurd codes are harmless" is "$CODE" 200

# ---- guests
post guest /new "category=2" "title=x" "body=y"
check "guest: POST /new is 403" is "$CODE" 403
check "guest: sign in first" has "$B" "E403 PLEASE SIGN IN FIRST"
post guest /t/6/reply "body=hello"
check "guest: reply is 403" has "$B" "E403 PLEASE SIGN IN FIRST"
get guest /new
check "guest: /new goes to /login, coming back" is "$LOC" "/login?next=/new"
from guest /t/6 "/t/6/reply?quote=17"
check "guest: quote goes to /login, coming back" is "$LOC" "/login?next=/t/6/reply?quote=17"
from guest /t/6 /p/17/edit
check "guest: edit goes to /login, coming back" is "$LOC" "/login?next=/p/17/edit"
get guest /settings
check "guest: settings goes to /login, coming back" is "$LOC" "/login?next=/settings"

# ---- new topic: editor, validation, preview, create
get tester "/new?c=2"
check "new: editor 200" is "$CODE" 200
check "new: category preselected" has "$B" '<option value="2" selected>'
post tester /new "category=0" "title=Hello" "body=World"
check "new: no category" has "$B" "E422 PLEASE CHOOSE A CATEGORY"
check "new: text kept" has "$B" 'value="Hello"'
post tester /new "category=2" "title=" "body=World"
check "new: empty title" has "$B" "E422 YOUR TOPIC HAS NO TITLE"
check "new: body kept" has "$B" ">World</textarea>"
post tester /new "category=2" "title=  	 " "body=World"
check "new: blank title" has "$B" "E422 YOUR TOPIC HAS NO TITLE"
t121=$(head -c 121 /dev/zero | tr '\0' T)
t120=$(head -c 120 /dev/zero | tr '\0' T)
post tester /new "category=2" "title=$t121" "body=World"
check "new: 121-byte title" has "$B" "E422 YOUR TITLE IS LONGER THAN 120 BYTES"
post tester /new "category=2" "title=$t120" "body=World" "preview=1"
check "new: 120-byte title is fine" hasnt "$B" "E422"
post tester /new "category=2" "title=Hello" "body="
check "new: empty body" has "$B" "E422 YOUR POST IS EMPTY"
post tester /new "category=2" "title=Hello" "body=
  
	"
check "new: blank body" has "$B" "E422 YOUR POST IS EMPTY"
head -c 20001 /dev/zero | tr '\0' b >"$TMP/big"
head -c 20000 /dev/zero | tr '\0' b >"$TMP/fits"
post_file tester /new body "$TMP/big" --data-urlencode "category=2" --data-urlencode "title=Big"
check "new: 20001-byte body" has "$B" "E422 YOUR POST IS LONGER THAN 20 000 BYTES"
post_file tester /new body "$TMP/fits" --data-urlencode "category=2" --data-urlencode "title=Big" \
    --data-urlencode "preview=1"
check "new: 20000-byte body is fine" hasnt "$B" "E422"
post tester /new "category=2" "title=Hello" "body=**strong** words" "preview=1"
check "new: preview 200" is "$CODE" 200
check "new: preview renders prose" has "$B" "<strong>strong</strong>"
check "new: preview keeps the text" has "$B" "**strong** words</textarea>"
post tester /new "category=2" "title=<script>alert(1)</script> & co" \
    "body=Hello *world*. <img src=x onerror=alert(1)> [x](javascript:alert(3)) @spark_ears"
check "new: create 303" is "$CODE" 303
T=${LOC#/t/}
T=${T%%\?*}
check "new: lands on the topic with a notice ($LOC)" is "$LOC" "/t/$T?done=4"
follow tester
check "notice: topic posted" noted "your topic was posted."
get tester "/t/$T"
check "new: topic 200" is "$CODE" 200
check "xss: title escaped" has "$B" "&lt;script&gt;alert(1)&lt;/script&gt; &amp; co"
check "xss: no script tag" hasnt "$B" "<script>alert(1)"
check "xss: body markup escaped" has "$B" "&lt;img src=x onerror=alert(1)&gt;"
check "xss: no img tag" hasnt "$B" "<img src=x"
check "xss: no javascript: link" hasnt "$B" 'href="javascript:'
check "prose: emphasis" has "$B" "<em>world</em>"
check "prose: mention" has "$B" 'class="mention" href="/u/spark_ears"'
P1=$(grep -o 'id="p[0-9]*"' "$B" | head -1 | tr -dc 0-9)
get guest /c/2
check "new: listed in its category" has "$B" "&lt;script&gt;alert(1)"

# ---- reply, permalink, quote
post tester "/t/$T/reply" "body=A reply with \`code\`"
check "reply: 303" is "$CODE" 303
P2=${LOC#/p/}
P2=${P2%%\?*}
check "reply: lands on its permalink with a notice" is "$LOC" "/p/$P2?done=5"
get tester "$LOC"
check "reply: the notice survives the permalink" is "$LOC" "/t/$T?page=1&done=5#p$P2"
follow tester
check "notice: reply posted" noted "your reply was posted."
get tester "/p/$P2"
check "reply: permalink page" is "$LOC" "/t/$T?page=1#p$P2"
get tester "/t/$T"
check "reply: shown" has "$B" "A reply with <code>code</code>"
post tester "/t/$T/reply" "body="
check "reply: empty body re-renders" has "$B" "E422 YOUR POST IS EMPTY"
post tester "/t/$T/reply" "body=Preview me" "preview=1"
check "reply: preview" has "$B" "<p>Preview me</p>"
from other "/t/$T" "/t/$T/reply?quote=$P2"
check "quote: 200" is "$CODE" 200
check "quote: prefilled" has "$B" '>[quote="tester"]'
check "quote: quoted body" has "$B" 'A reply with `code`'
from other "/t/$T" "/t/$T/reply?quote=17"
check "quote: a post from another topic is 404" has "$B" "E404 NO SUCH POST"
post other "/t/$T/reply" "body=[quote=\"tester\"]
quoted
[/quote]

Agreed."
check "quote: reply posted" is "$CODE" 303
get other "/t/$T"
check "quote: rendered as a block quote" has "$B" 'class="bbquote"'

# ---- PLEASE
post tester "/p/$P2/please"
check "please: own post is 403" is "$CODE" 403
check "please: overly polite" has "$B" "YOU CANNOT PLEASE YOURSELF. THAT WOULD BE OVERLY POLITE"
post other "/p/$P2/please"
check "please: 303" is "$LOC" "/p/$P2"
get other "/t/$T"
check "please: pressed" has "$B" 'aria-pressed="true"'
check "please: counted" has "$B" '<span class="please-n" title="1">I</span>'
get guest "/u/tester"
check "please: received on the profile" has "$B" 'title="1">I<'
post other "/p/$P2/please"
get other "/t/$T"
check "un-please: released" hasnt "$B" 'aria-pressed="true"'
check "un-please: uncounted" hasnt "$B" '<span class="please-n" title="1">I</span>'
post other "/p/99999/please"
check "please: missing post is 404" is "$CODE" 404

# ---- edit
from tester "/t/$T" "/p/$P2/edit"
check "edit: editor 200" is "$CODE" 200
check "edit: body loaded" has "$B" 'A reply with `code`</textarea>'
check "edit: a reply has no title field" hasnt "$B" 'name="title"'
post tester "/p/$P2/edit" "body=Edited reply"
check "edit: 303 to the post" is "$LOC" "/p/$P2?done=6"
follow tester
check "notice: edit saved" noted "your changes were saved."
get tester "/t/$T"
check "edit: new body shown" has "$B" "<p>Edited reply</p>"
check "edit: marked edited" has "$B" 'class="edited"'
from tester "/t/$T" "/p/$P1/edit"
check "edit: opening post edits the title" has "$B" 'name="title"'
post tester "/p/$P1/edit" "title=Renamed topic" "body=New opening"
get tester "/t/$T"
check "edit: title changed" has "$B" "<title>Renamed topic"
post tester "/p/$P1/edit" "title=" "body=New opening"
check "edit: empty title rejected" has "$B" "E422 YOUR TOPIC HAS NO TITLE"
from tester /t/6 /p/17/edit
check "edit: someone else's post (GET) is 403" is "$CODE" 403
check "edit: not yours" has "$B" "E403 THAT POST IS NOT YOURS"
post tester /p/17/edit "body=mine now"
check "edit: someone else's post (POST) is 403" is "$CODE" 403
from mod /t/6 /p/17/edit
check "edit: moderators may edit any post" is "$CODE" 200

# ---- delete
post other "/p/$P2/delete"
check "delete: someone else's post is 403" has "$B" "E403 THAT POST IS NOT YOURS"
post tester "/p/$P2/delete"
check "delete: 303" is "$LOC" "/p/$P2?done=7"
follow tester
check "notice: deleted" noted "the post was ABSTAINED FROM (deleted)."
get tester "/t/$T"
check "delete: author tombstone" has "$B" "ABSTAINED FROM by its author"
check "delete: body gone" hasnt "$B" "Edited reply"
post tester "/p/$P2/delete"
check "delete: twice is 404" has "$B" "E404 NO SUCH POST"
post other "/p/$P2/please"
check "delete: no PLEASE for tombstones" is "$CODE" 404
get tester "/p/$P2"
check "delete: permalink still works" is "$CODE" 303
get tester "/t/$T"
QP=$(grep -o 'id="p[0-9]*"' "$B" | tail -1 | tr -dc 0-9)
post mod "/p/$QP/delete"
get tester "/t/$T"
check "delete: moderator tombstone" has "$B" "ABSTAINED FROM by a moderator"

# ---- moderation: pin, lock, move
post tester "/t/$T/pin" "pinned=1"
check "pin: members get 403" has "$B" "E403 ONLY MODERATORS MAY DO THAT"
post mod "/t/$T/pin" "pinned=1"
check "pin: 303 to the topic" is "$LOC" "/t/$T?done=8"
follow mod
check "notice: pinned" noted "the topic is pinned to the top of its category."
get mod "/t/$T"
check "pin: pinned" has "$B" 'flag-pin'
get guest /c/2
first=$(grep -o 'href="/t/[0-9]*"' "$B" | head -1)
check "pin: pinned first in the category" is "$first" "href=\"/t/$T\""
post mod "/t/$T/pin" "pinned=0"
check "unpin: notice code" is "$LOC" "/t/$T?done=9"
follow mod
check "notice: unpinned" noted "the topic is no longer pinned."
get mod "/t/$T"
check "unpin: unpinned" hasnt "$B" 'flag-pin'
post mod "/t/$T/lock" "locked=1"
follow mod
check "notice: locked" noted "the topic is locked. No more replies."
get tester "/t/$T"
check "lock: locked notice" has "$B" "This topic is locked"
check "lock: no reply form" hasnt "$B" 'id="reply-body"'
check "lock: no Quote" hasnt "$B" "/reply?quote="
post tester "/t/$T/reply" "body=let me in"
check "lock: reply is 403" is "$CODE" 403
check "lock: in voice" has "$B" "E403 THIS TOPIC IS LOCKED"
post mod "/t/$T/reply" "body=moderators too"
check "lock: moderators cannot reply either" is "$CODE" 403
from tester "/t/$T" "/t/$T/reply?quote=$P1"
check "lock: quote is 403" is "$CODE" 403
post tester "/p/$P1/edit" "title=Still editable" "body=Edits are fine"
check "lock: editing still works" is "$CODE" 303
post mod "/t/$T/lock" "locked=0"
follow mod
check "notice: unlocked" noted "the topic is unlocked. Replies are open again."
post tester "/t/$T/reply" "body=unlocked again"
check "unlock: replies work again" is "$CODE" 303
post mod "/t/$T/move" "category=4"
check "move: 303" is "$LOC" "/t/$T?done=12"
follow mod
check "notice: moved" noted "the topic was moved."
get guest "/t/$T"
check "move: now in category 4" has "$B" 'class="badge-cat" style="--cat: '
check "move: breadcrumb" has "$B" 'href="/c/4">ABSTAINed (off-topic)</a>'
post mod "/t/$T/move" "category=99"
check "move: missing category is 404" has "$B" "E404 NO SUCH CATEGORY"
post mod "/t/99999/lock" "locked=1"
check "lock: missing topic is 404" has "$B" "E404 NO SUCH TOPIC"
post tester "/t/$T/move" "category=1"
check "move: members get 403" is "$CODE" 403

# ---- roles
post tester /u/come_from/role "role=2"
check "role: members get 403" has "$B" "E403 ONLY THE ADMIN MAY DO THAT"
post admin /u/TESTER/role "role=7"
check "role: unknown role is 400" is "$CODE" 400
check "role: in voice" has "$B" "E400 THAT ROLE DOES NOT EXIST"
post admin /u/admin/role "role=1"
check "role: admins are fixed" has "$B" "E403 ADMINS CANNOT BE PROMOTED OR DEMOTED"
post admin /u/nobody_here/role "role=2"
check "role: missing user is 404" has "$B" "E404 NO SUCH USER"
post admin /u/TESTER/role "role=2"
check "role: promote 303 to the canonical name" is "$LOC" "/u/tester?done=13"
follow admin
check "notice: promoted" noted "that user is now a moderator."
get guest /u/tester
check "role: now a moderator" has "$B" 'class="role role-2"'
post tester "/t/6/pin" "pinned=1"
check "role: the new moderator can pin" is "$CODE" 303
post tester "/t/6/pin" "pinned=0"
post admin /u/tester/role "role=1"
follow admin
check "notice: demoted" noted "that user is now a member."
get guest /u/tester
check "role: demoted" has "$B" 'class="role role-1"'

# ---- settings (bio)
get tester /settings
check "settings: 200" is "$CODE" 200
post tester /settings "bio=I like <script>alert(2)</script> and **COME FROM**"
check "settings: 303 to the profile" is "$LOC" "/u/tester?done=15"
follow tester
check "notice: bio saved" noted "your bio was saved."
get guest /u/tester
check "xss: bio escaped" has "$B" "&lt;script&gt;alert(2)&lt;/script&gt;"
check "xss: no script in bio" hasnt "$B" "<script>alert(2)"
check "settings: bio as prose" has "$B" "<strong>COME FROM</strong>"
head -c 2001 /dev/zero | tr '\0' z >"$TMP/bio"
post_file tester /settings bio "$TMP/bio"
check "settings: long bio" has "$B" "E422 YOUR BIO IS LONGER THAN 2 000 BYTES"
post tester /settings "bio=Previewed *bio*" "preview=1"
check "settings: preview" has "$B" "<em>bio</em>"
post guest /settings "bio=x"
check "settings: guests get 403" is "$CODE" 403

# ---- the session survives all of the above
get tester /
check "session: still signed in" has "$B" '<span class="me-name">tester</span>'

# ---- speed
t=$(curl -s -o /dev/null -w '%{time_total}' "$U/t/12")
echo "topic page /t/12: ${t}s"
check "speed: topic page under 100 ms" awk "BEGIN{exit !($t < 0.1)}"

stop_gross
echo "forum: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
