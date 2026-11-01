NOTE KITCHEN SINK: A GUESTBOOK WRITTEN IN INTERCAL ON INTERSTATES.
NOTE Every Interstates construct appears at least once. Compile with
NOTE ick -s -b kitchen_sink.i and serve with gross; run from a shell
NOTE and it performs the self-test below, top to bottom, as plain INTERCAL.
NOTE
NOTE A MAP OF THE INTERSTATE (route, origin, what happens):
PLEASE NOTE   GET  /                       16000  the guestbook, newest first
PLEASE NOTE   POST /sign                   (110)  sign it; empty messages bounce
PLEASE NOTE   GET  /entry/:id              16000  one entry, or a 404 page
PLEASE NOTE   POST /like/:id               (110)  PLEASE an entry (adds one like)
PLEASE NOTE   POST /entry/:id/delete       (300)  FORGET an entry
PLEASE NOTE   POST /entry/:id/redact       (300)  update two columns at once
PLEASE NOTE   GET  /u/:name                16000  a user's page
PLEASE NOTE   GET  /login                  16000  the sign-in form
PLEASE NOTE   POST /login                  (200)  check the PASSWORD column
PLEASE NOTE   POST /logout                 (110)  GIVE UP the session
PLEASE NOTE   GET  /search?q=&skip=        16000  SOMEWHERE, BEYOND, SKIP, TAKE
PLEASE NOTE   GET  /files/*                16000  a splat parameter, text/plain
PLEASE NOTE   GET  /time                   16000  WRITE IN WITH TIME
PLEASE NOTE   GET  /api/stats              16000  application/json from a view
PLEASE NOTE   GET  /api/entries            16000  a JSON list built by a loop
NOTE   *                            16000  everything else: our own 404
NOTE
NOTE Origins other than 16000 mean "only from that page": the browser must
NOTE send an Origin or Referer naming this host, or the request is refused
NOTE with E403. Every POST needs one as well.
NOTE
NOTE The branch idiom used throughout: compute .3 as #1 or #2, then
NOTE    DO (90x) NEXT          -- falls through here when .3 is #2
NOTE    ...
NOTE    (90x) DO (999) NEXT    -- returns here when .3 is #1
NOTE    DO FORGET #1
NOTE    ...
NOTE    (999) DO RESUME .3

PLEASE NOTE ===== SHELL MODE: VALUE CONVERSIONS AND CLASSIC STATEMENTS =====
DO ,1 <- "123"
DO .5 <- ,1
DO READ OUT .5
DO ;2 <- #2
DO ;2 SUB #1 <- "four"
DO ;2 SUB #2 <- #4000
DO ;3 <- ;2
PLEASE DO ,4 <- ;3 SUB #1
DO .6 <- ;3 SUB #2
DO READ OUT .6
NOTE STASH-assign grows a never-dimensioned array; it does not touch the stash.
DO STASH ,7 SUB #3 <- #65
DO .7 <- ,7 SUB #3
PLEASE READ OUT .7
NOTE STASH-assign to a scalar is just an assignment, and a string literal
NOTE assigned to a scalar is read as a decimal number.
DO STASH .7 <- "66"
DO READ OUT .7
NOTE Ordinary STASH and RETRIEVE still work.
DO STASH .5
DO .5 <- #9
DO RETRIEVE .5
DO READ OUT .5
NOTE IGNORE makes the next assignment vanish; REMEMBER undoes that.
PLEASE IGNORE .6
DO .6 <- #1
DO REMEMBER .6
DO READ OUT .6
NOTE ABSTAIN by label and by gerund, then REINSTATE.
DO ABSTAIN FROM (20)
(20) DO .8 <- #1
DO .8 <- #2
PLEASE REINSTATE (20)
DO READ OUT .8
DO ABSTAIN FROM CALCULATING
DO .8 <- #3
DO REINSTATE CALCULATING
DO READ OUT .8
NOTE The system library is appended because we NEXT to (1000).
DO .1 <- #5
DO .2 <- #7
PLEASE DO (1000) NEXT
DO READ OUT .3
NOTE A classic COME FROM (not a route) works in ordinary control flow.
(30) DO .9 <- #11
DO READ OUT #1
(31) DO COME FROM (30)
DO READ OUT .9
NOTE A database read in a shell: the primary key lookup of view row 1.
DO .90 <- 'FROM DATABASE #2~#1' SUB #1
DO READ OUT .90
NOTE Templates render to standard output when no request is present.
PLEASE READ OUT #0 WITH TEMPLATE "ks_shell.txt"
PLEASE GIVE UP

PLEASE NOTE ===== THE INDEX PAGE =====
DO COME FROM (16000)
DO WRITE IN .10 WITH SESSION
DO STASH ;49221 SUB #1 <- 'FROM DATABASE #1~#0$#2$#3$#4$#1' BY #3 BACKWARDS BY #0 TAKE #20 IN BULK
DO STASH ;49221 SUB #2 <- 'FROM DATABASE #1' HOW MANY
DO STASH ;49221 SUB #3 <- 'FROM DATABASE #0~#1' HAVING #0 .10 HAVING #0 BEYOND #0
DO STASH ;49221 SUB #4 <- 'FROM DATABASE #2~#1'
(110) PLEASE READ OUT #4 WITH TEMPLATE "ks_index.html"
DO GIVE UP

PLEASE NOTE ===== SIGNING THE GUESTBOOK (ONLY FROM THE INDEX PAGE) =====
DO COME FROM (110) WITH ROUTE "POST /sign"
DO WRITE IN ,20 WITH FIELD "message"
NOTE .3 becomes #2 when the message has bytes, #1 when it is empty.
DO .1 <- '.4401~.4401'~#1
DO .2 <- #1
DO (1000) NEXT
DO (901) NEXT
NOTE .3 was #2: the message is present. Author is the session user.
DO WRITE IN .10 WITH SESSION
PLEASE DO ;30 <- #4
DO ;30 SUB #1 <- 'FROM DATABASE #0~#1' HAVING #0 .10
DO ;30 SUB #2 <- ,20
DO WRITE IN :5 WITH TIME
DO ;30 SUB #3 <- :5
DO ;30 SUB #4 <- #0
PLEASE STASH ;30 INTO DATABASE #1
DO STASH ;49221 SUB #1 <- :4400
DO READ OUT #1 WITH REDIRECT "ks_to_entry.txt"
DO GIVE UP
(901) DO (999) NEXT
PLEASE FORGET #1
NOTE .3 was #1: the message is empty. Say so, politely.
DO STASH ;49221 SUB #1 <- "YOUR MESSAGE IS EMPTY. PLEASE CORRECT SOURCE AND RESUBNIT"
DO READ OUT #1 WITH TEMPLATE "ks_error.html" WITH STATUS #400
PLEASE GIVE UP

NOTE (999) is the shared branch helper: RESUME #1 or #2 picks the path.
(999) DO RESUME .3

PLEASE NOTE ===== ONE ENTRY =====
DO COME FROM (16000) WITH ROUTE "/entry/:id"
DO ,40 <- ;4400 SUB #1
DO .1 <- 'FROM DATABASE #1' HAVING #0 ,40 HOW MANY
DO .1 <- '.1~.1'~#1
DO .2 <- #1
PLEASE DO (1000) NEXT
DO (902) NEXT
NOTE Found it: the row becomes the template parameters.
DO ;49221 <- 'FROM DATABASE #1~#0$#2$#3$#4$#1' HAVING #0 ,40
(300) DO READ OUT #5 WITH TEMPLATE "ks_entry.html"
PLEASE GIVE UP
(902) DO (999) NEXT
DO FORGET #1
DO READ OUT #0 WITH TEMPLATE "ks_missing.html" WITH STATUS #404
PLEASE GIVE UP

PLEASE NOTE ===== LIKING AN ENTRY (A PLEASE) =====
DO COME FROM (110) WITH ROUTE "POST /like/:id"
DO ,50 <- ;4400 SUB #1
DO .1 <- 'FROM DATABASE #1~#4' HAVING #0 ,50
DO .2 <- #1
DO (1000) NEXT
PLEASE DO 'FROM DATABASE #1~#4' HAVING #0 ,50 <- .3
DO STASH ;49221 SUB #1 <- ,50
DO READ OUT #1 WITH REDIRECT "ks_to_entry.txt"
DO GIVE UP

PLEASE NOTE ===== DELETING AN ENTRY (ONLY FROM ITS OWN PAGE) =====
DO COME FROM (300) WITH ROUTE "POST /entry/:id/delete"
DO ,60 <- ;4400 SUB #1
PLEASE FORGET 'FROM DATABASE #1' HAVING #0 ,60
DO READ OUT #0 WITH REDIRECT "ks_to_root.txt"
DO GIVE UP

PLEASE NOTE ===== SIGNING IN =====
DO COME FROM (16000) WITH ROUTE "/login"
(200) PLEASE READ OUT #0 WITH TEMPLATE "ks_login.html"
DO GIVE UP

DO COME FROM (200) WITH ROUTE "POST /login"
DO WRITE IN ,70 WITH FIELD "name"
PLEASE WRITE IN ,71 WITH FIELD "password"
DO .1 <- 'FROM DATABASE #0' HAVING #1 ,70 HAVING #2 ,71 HOW MANY
DO .1 <- '.1~.1'~#1
DO .2 <- #1
DO (1000) NEXT
PLEASE DO (903) NEXT
NOTE The password matched: the session remembers the user id.
DO .71 <- 'FROM DATABASE #0~#0' HAVING #1 ,70
DO READ OUT .71 WITH SESSION
DO READ OUT #0 WITH REDIRECT "ks_to_root.txt"
DO GIVE UP
(903) DO (999) NEXT
PLEASE FORGET #1
DO STASH ;49221 SUB #1 <- "PASSWORD REJECTED. YOU CAN'T GET THERE FROM HERE"
DO READ OUT #1 WITH TEMPLATE "ks_error.html" WITH STATUS #403
DO GIVE UP

DO COME FROM (110) WITH ROUTE "POST /logout"
PLEASE READ OUT #0 WITH SESSION
DO READ OUT #0 WITH REDIRECT "ks_to_root.txt"
DO GIVE UP

PLEASE NOTE ===== SEARCH, WITH SOMEWHERE, BEYOND, SKIP AND TAKE =====
DO COME FROM (16000) WITH ROUTE "/search"
DO WRITE IN ,80 WITH FIELD "q"
DO WRITE IN .81 WITH FIELD "skip"
DO STASH ;49221 SUB #1 <- ,80
DO STASH ;49221 SUB #2 <- 'FROM DATABASE #1~#0$#2' HAVING #2 SOMEWHERE ,80 BY #0 SKIP .81 TAKE #10 IN BULK
PLEASE STASH ;49221 SUB #3 <- 'FROM DATABASE #1' HAVING #2 SOMEWHERE ,80 HOW MANY
DO STASH ;49221 SUB #4 <- 'FROM DATABASE #1' HAVING #4 BEYOND #0 HOW MANY
DO READ OUT #4 WITH TEMPLATE "ks_search.html"
DO GIVE UP

PLEASE NOTE ===== A SPLAT ROUTE, PLAIN TEXT AND JSON =====
DO COME FROM (16000) WITH ROUTE "/files/*"
DO STASH ;49221 SUB #1 <- ;4400 SUB #1
DO STASH ;49221 SUB #2 <- "a splat"
DO READ OUT #2 WITH TEMPLATE "ks_file.txt"
PLEASE GIVE UP

DO COME FROM (16000) WITH ROUTE "/time"
DO WRITE IN :1 WITH TIME
DO STASH ;49221 SUB #1 <- :1
DO READ OUT #1 WITH TEMPLATE "ks_time.txt"
DO GIVE UP

DO COME FROM (16000) WITH ROUTE "GET /api/stats"
DO ;49221 <- 'FROM DATABASE #2~#1'
PLEASE DO .90 <- 'FROM DATABASE #1~#0' BY #0 TAKE #1
DO READ OUT #2 WITH TEMPLATE "ks_stats.json"
DO GIVE UP

PLEASE NOTE ===== A JSON LIST BUILT BY A TEMPLATE LOOP =====
DO COME FROM (16000) WITH ROUTE "/api/entries"
DO STASH ;49221 SUB #1 <- 'FROM DATABASE #1~#0$#4' BY #0 IN BULK
DO READ OUT #1 WITH TEMPLATE "ks_entries.json"
PLEASE GIVE UP

PLEASE NOTE ===== A USER'S PAGE =====
DO COME FROM (16000) WITH ROUTE "/u/:name"
DO ,90 <- ;4400 SUB #1
DO .1 <- 'FROM DATABASE #0' HAVING #1 ,90 HOW MANY
DO .1 <- '.1~.1'~#1
DO .2 <- #1
DO (1000) NEXT
PLEASE DO (904) NEXT
NOTE The user exists: name, joining time, entry count and entries.
DO STASH ;49221 SUB #1 <- ,90
DO STASH ;49221 SUB #2 <- 'FROM DATABASE #0~#3' HAVING #1 ,90
DO STASH ;49221 SUB #3 <- 'FROM DATABASE #1' HAVING #1 ,90 HOW MANY
DO STASH ;49221 SUB #4 <- 'FROM DATABASE #1~#0$#2' HAVING #1 ,90 BY #0 IN BULK
PLEASE READ OUT #4 WITH TEMPLATE "ks_user.html"
DO GIVE UP
(904) DO (999) NEXT
DO FORGET #1
DO READ OUT #0 WITH TEMPLATE "ks_missing.html" WITH STATUS #404
PLEASE GIVE UP

PLEASE NOTE ===== REDACTING: ONE UPDATE, TWO COLUMNS =====
DO COME FROM (300) WITH ROUTE "POST /entry/:id/redact"
DO ,95 <- ;4400 SUB #1
DO ;95 <- #2
DO ;95 SUB #1 <- "[redacted by a moderator]"
DO ;95 SUB #2 <- #0
PLEASE DO 'FROM DATABASE #1~#2$#4' HAVING #0 ,95 <- ;95
DO STASH ;49221 SUB #1 <- ,95
DO READ OUT #1 WITH REDIRECT "ks_to_entry.txt"
DO GIVE UP

PLEASE NOTE ===== PAGES OF TWO ENTRIES, ARITHMETIC BY THE SYSTEM LIBRARY =====
NOTE /page/1 shows entries 1-2, /page/2 entries 3-4, and so on.
DO COME FROM (16000) WITH ROUTE "/page/:n"
DO .1 <- ;4400 SUB #1
DO .2 <- #1
NOTE (1010) subtracts: .3 <- .1 - .2
DO (1010) NEXT
DO .1 <- .3
DO .2 <- #2
NOTE (1030) multiplies: .3 <- .1 times .2
PLEASE DO (1030) NEXT
DO STASH ;49221 SUB #1 <- 'FROM DATABASE #1~#0$#2' BY #0 SKIP .3 TAKE #2 IN BULK
DO .1 <- ;4400 SUB #1
DO .2 <- #1
DO (1000) NEXT
DO STASH ;49221 SUB #2 <- .3
DO STASH ;49221 SUB #3 <- ;4400 SUB #1
PLEASE READ OUT #3 WITH TEMPLATE "ks_page.html"
DO GIVE UP

PLEASE NOTE ===== FIELDS: DIGIT WORDS, BYTES AND THEIR LENGTH =====
DO COME FROM (16000) WITH ROUTE "/echo"
DO WRITE IN .20 WITH FIELD "n"
DO WRITE IN ,21 WITH FIELD "text"
DO .21 <- .4401
DO WRITE IN .22 WITH FIELD "absent"
PLEASE READ OUT #0 WITH TEMPLATE "ks_echo.txt"
DO GIVE UP

PLEASE NOTE ===== ONLY FOR THE SIGNED IN =====
DO COME FROM (16000) WITH ROUTE "/me"
DO WRITE IN .10 WITH SESSION
DO .1 <- '.10~.10'~#1
DO .2 <- #1
DO (1000) NEXT
PLEASE DO (905) NEXT
DO STASH ;49221 SUB #1 <- 'FROM DATABASE #0~#1' HAVING #0 .10
DO READ OUT #1 WITH REDIRECT "ks_to_user.txt"
DO GIVE UP
(905) DO (999) NEXT
DO FORGET #1
DO STASH ;49221 SUB #1 <- "SIGN IN FIRST. YOU CAN'T GET THERE FROM HERE"
PLEASE READ OUT #1 WITH TEMPLATE "ks_error.html" WITH STATUS #403
DO GIVE UP

PLEASE NOTE ===== EVERYTHING ELSE =====
DO COME FROM (16000) WITH ROUTE "*"
DO READ OUT #0 WITH TEMPLATE "ks_missing.html" WITH STATUS #404
PLEASE GIVE UP
