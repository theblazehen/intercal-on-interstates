PLEASE NOTE ================================================================
NOTE PLEASE DISCUSS: THE INTERCAL ON INTERSTATES COMMUNITY FORUM
NOTE Compile with ick -s -b forum.i and serve with gross ./forum.
NOTE The contract (routes, origins, tables, template parameters,
NOTE messages) is ROUTES.md; this file follows it line for line.
PLEASE NOTE ================================================================
NOTE
NOTE A MAP OF THE INTERSTATE. Route n lives at labels (2000 + 100n).
NOTE    route                      origin  labels  answers
PLEASE NOTE  1 GET  /                     16000   21xx    board index (100)
NOTE  2 GET  /latest               16000   22xx    latest topics (110)
NOTE  3 GET  /c/:id                16000   23xx    a category (120)
NOTE  4 GET  /t/:id                16000   24xx    a topic (130)
NOTE  5 GET  /p/:id                16000   25xx    permalink, 303 to its page
PLEASE NOTE  6 GET  /t/:id/reply          (130)   26xx    reply editor (140)
NOTE  7 POST /t/:id/reply          16000   27xx    post a reply
NOTE  8 GET  /new                  16000   28xx    new topic editor (140)
NOTE  9 POST /new                  (140)   29xx    create a topic
PLEASE NOTE 10 GET  /p/:id/edit          (130)   30xx    edit editor (140)
NOTE 11 POST /p/:id/edit          (140)   31xx    save an edit
NOTE 12 POST /p/:id/delete        (130)   32xx    soft delete
NOTE 13 POST /p/:id/please        (130)   33xx    PLEASE toggle
PLEASE NOTE 14 POST /t/:id/pin           (130)   34xx    pin or unpin
NOTE 15 POST /t/:id/lock          (130)   35xx    lock or unlock
NOTE 16 POST /t/:id/move          (130)   36xx    move to another category
NOTE 17 GET  /u/:name             16000   37xx    profile (170)
PLEASE NOTE 18 POST /u/:name/role        (170)   38xx    promote or demote
NOTE 19 GET  /settings            16000   39xx    bio editor (180)
NOTE 20 POST /settings            (180)   40xx    save the bio
NOTE 21 GET  /search              16000   41xx    search (190)
PLEASE NOTE 22 GET  /login               16000   42xx    sign in form (150)
NOTE 23 POST /login               (150)   43xx    sign in
NOTE 24 GET  /register            16000   44xx    register form (160)
NOTE 25 POST /register            (160)   45xx    register
PLEASE NOTE 26 POST /logout              16000   46xx    GIVE UP (sign out)
NOTE 27 *                         16000   47xx    E404 NO SUCH PAGE
NOTE
NOTE OTHER LABELS
PLEASE NOTE  (100)-(190)  the one READ OUT line of each page template
NOTE  (400) (403) (404)  error.html with status 400, 403, 404
NOTE  (600)-(690)  form validators (category, title, body, bio,
NOTE               username, password, confirmation, save-or-show)
NOTE  (700)-(771)  string building: the return path ,101 for Sign in
NOTE               links and next=, and ?done=N notices
PLEASE NOTE  (800)-(894)  error raisers: E404 NO SUCH TOPIC and friends
NOTE  (900)-(989)  subroutines: session, guards, pagination, byte
NOTE               scanning, comparisons and the branch helper
NOTE  (1000)-(1999) the system library, appended by ick
PLEASE NOTE
NOTE THE BRANCH IDIOM. (980) is the only conditional:
NOTE     DO .5 <- something
NOTE     DO (L) NEXT
NOTE     ... carries on here when .5 is not zero
PLEASE NOTE     (L) DO (980) NEXT
NOTE     ... runs when .5 is zero; RESUME #1 returns to the
NOTE         line after DO (L) NEXT (IF ... THEN), RESUME #2
NOTE         returns from the subroutine that did DO (L) NEXT,
PLEASE NOTE         and error raisers never return at all.
NOTE Conditions are computed as 0/1 flags: '.x~.x'~#1 is 1 when .x
NOTE is not zero, '?.a$.b'~'#0$#65535' is .a XOR .b, 'V.a$.b'~'#0$#65535'
NOTE is .a OR .b, (960) compares two 32-bit numbers for difference and
PLEASE NOTE (970) for less-than. Arithmetic is the system library.
NOTE
NOTE VARIABLES. Templates read :100 ,100 .101-.105 ,101 ;100 (ROUTES.md 1.2)
NOTE and READ OUT destroys ,120-,144 ;120-;144 .144. Handlers use
PLEASE NOTE :9 now, :10 the route id, :11-:14 other ids, ,10-,12 text fields,
NOTE .42-.46 their byte lengths, .40 form-still-valid, ,9 the validation
NOTE message, ,7 the error message, :20-:26 pagination, ;30-;36 rows,
NOTE ,70 .70-.75 :72-:76 string building (:76 is the notice to send).

PLEASE NOTE ===== ROUTE 1: GET / -- THE BOARD INDEX =====
NOTE Run from a shell, the program falls in here and prints the board
NOTE index, which also creates and seeds a fresh database.
DO COME FROM (16000) WITH ROUTE "/"
DO (900) NEXT
NOTE A Sign in link here comes back to the board index.
DO ,101 <- "/"
PLEASE DO .71 <- #1
DO .104 <- #1
PLEASE DO ;49221 <- #2
DO ;49221 SUB #1 <- 'FROM DATABASE #6~#0$#2$#4$#5$#6$#7$#8$#9$#10$#12$#13$#14' BY #3 BY #0 IN BULK
DO ;49221 SUB #2 <- 'FROM DATABASE #11~#1$#2$#3$#4$#5' HAVING #0 #1
(100) DO READ OUT #2 WITH TEMPLATE "index.html"
PLEASE GIVE UP

PLEASE NOTE ===== ROUTE 2: GET /latest -- EVERY TOPIC BY LAST ACTIVITY =====
DO COME FROM (16000) WITH ROUTE "/latest"
DO (900) NEXT
DO .104 <- #2
DO :20 <- 'FROM DATABASE #7~#0' HAVING #0 BEYOND #0 HOW MANY
PLEASE WRITE IN :21 WITH FIELD "page"
DO (910) NEXT
DO ,101 <- "/latest"
DO .71 <- #7
PLEASE DO (750) NEXT
DO ;49221 <- #5
DO ;49221 SUB #1 <- 'FROM DATABASE #7~#0$#4$#1$#2$#3$#6$#16$#17$#18$#19$#12$#11$#8$#9$#10$#14' BY #8 BACKWARDS BY #0 BACKWARDS SKIP :26 TAKE #20 IN BULK
DO ;49221 SUB #2 <- :22
PLEASE DO ;49221 SUB #3 <- :23
DO ;49221 SUB #4 <- :24
DO ;49221 SUB #5 <- :25
(110) DO READ OUT #5 WITH TEMPLATE "latest.html"
PLEASE GIVE UP

PLEASE NOTE ===== ROUTE 3: GET /c/:id -- ONE CATEGORY, PINNED FIRST =====
DO COME FROM (16000) WITH ROUTE "/c/:id"
DO (900) NEXT
DO WRITE IN :10 WITH FIELD "id"
PLEASE DO .5 <- 'FROM DATABASE #2~#0' HAVING #0 :10 HOW MANY
DO (2301) NEXT
DO :20 <- 'FROM DATABASE #7~#0' HAVING #1 :10 HOW MANY
DO WRITE IN :21 WITH FIELD "page"
DO (910) NEXT
DO ,101 <- "/c/"
DO .71 <- #3
DO :72 <- :10
PLEASE DO (710) NEXT
DO (750) NEXT
PLEASE DO ;49221 <- 'FROM DATABASE #6~#0$#5$#6$#7$#2$#8' HAVING #0 :10
DO STASH ;49221 SUB #7 <- 'FROM DATABASE #7~#0$#4$#1$#2$#3$#6$#16$#17$#18$#19$#12$#11$#8$#9$#10$#14' HAVING #1 :10 BY #9 BACKWARDS BY #8 BACKWARDS BY #0 BACKWARDS SKIP :26 TAKE #20 IN BULK
DO STASH ;49221 SUB #8 <- :22
DO STASH ;49221 SUB #9 <- :23
PLEASE STASH ;49221 SUB #10 <- :24
DO STASH ;49221 SUB #11 <- :25
(120) DO READ OUT #11 WITH TEMPLATE "category.html"
DO GIVE UP
(2301) PLEASE DO (980) NEXT
DO (803) NEXT

PLEASE NOTE ===== ROUTE 4: GET /t/:id -- A TOPIC AND ONE PAGE OF POSTS =====
DO COME FROM (16000) WITH ROUTE "/t/:id"
DO (900) NEXT
DO WRITE IN :10 WITH FIELD "id"
DO (917) NEXT
NOTE Count the view before anything else: views <- views + 1.
PLEASE DO :1 <- 'FROM DATABASE #3~#8' HAVING #0 :10
DO :2 <- #1
DO (1500) NEXT
DO 'FROM DATABASE #3~#8' HAVING #0 :10 <- :3
NOTE Every post counts for paging, tombstones included.
PLEASE DO :20 <- 'FROM DATABASE #4~#0' HAVING #1 :10 HOW MANY
DO WRITE IN :21 WITH FIELD "page"
DO (910) NEXT
DO ,101 <- "/t/"
DO .71 <- #3
DO :72 <- :10
PLEASE DO (710) NEXT
DO (750) NEXT
DO ;30 <- 'FROM DATABASE #8~#0$#2$#3$#4$#5$#6$#7$#8$#9$#10$#11$#12$#13$#1$#15' HAVING #1 :10 BY #0 SKIP :26 TAKE #20 IN BULK
DO ;49221 <- 'FROM DATABASE #7~#0$#4$#1$#2$#3$#9$#10$#11$#13$#20' HAVING #0 :10
PLEASE STASH ;49221 SUB #11 <- ;30
DO STASH ;49221 SUB #12 <- :22
DO STASH ;49221 SUB #13 <- :23
DO STASH ;49221 SUB #14 <- :24
DO STASH ;49221 SUB #15 <- :25
PLEASE STASH ;49221 SUB #16 <- 'FROM DATABASE #6~#0$#5' BY #3 BY #0 IN BULK
NOTE ;100 SUB k: did the reader PLEASE post row k? Guests look at no rows.
DO ;100 <- #20
DO .1 <- 'FROM DATABASE #4~#0' HAVING #1 :10 SKIP :26 TAKE #20 HOW MANY
DO .2 <- ':100~:100'~#1
PLEASE DO (1030) NEXT
DO .30 <- .3
DO .31 <- #0
DO COME FROM (2409)
DO .5 <- '?.31$.30'~'#0$#65535'
PLEASE DO (2402) NEXT
DO .1 <- .31
DO (1020) NEXT
DO .31 <- .1
DO :32 <- ';30 SUB .31' SUB #1
(2409) DO ;100 SUB .31 <- 'FROM DATABASE #5~#0' HAVING #1 :32 HAVING #2 :100 HOW MANY
(2402) PLEASE DO (980) NEXT
DO FORGET #1
(130) DO READ OUT #16 WITH TEMPLATE "topic.html"
DO GIVE UP

PLEASE NOTE ===== ROUTE 5: GET /p/:id -- PERMALINK =====
NOTE page = (number + 19) / 20; deleted posts keep their permalink.
DO COME FROM (16000) WITH ROUTE "/p/:id"
DO WRITE IN :10 WITH FIELD "id"
NOTE A notice code (done) travels on to the topic page.
PLEASE WRITE IN :15 WITH FIELD "done"
DO .5 <- 'FROM DATABASE #4~#0' HAVING #0 :10 HOW MANY
PLEASE DO (2501) NEXT
DO ;31 <- 'FROM DATABASE #8~#1$#13' HAVING #0 :10
DO :1 <- ;31 SUB #2
DO :2 <- #19
DO (1500) NEXT
PLEASE DO :1 <- :3
DO :2 <- #20
DO (1550) NEXT
DO ;49221 <- #4
DO ;49221 SUB #1 <- ;31 SUB #1
PLEASE DO ;49221 SUB #2 <- :3
DO ;49221 SUB #3 <- :10
DO ;49221 SUB #4 <- :15
DO READ OUT #4 WITH REDIRECT "to_topic_page.txt"
DO GIVE UP
(2501) DO (980) NEXT
PLEASE DO (900) NEXT
DO (802) NEXT

PLEASE NOTE ===== ROUTE 6: GET /t/:id/reply -- THE REPLY EDITOR, MAYBE QUOTING =====
DO COME FROM (130) WITH ROUTE "/t/:id/reply"
DO (900) NEXT
DO WRITE IN :10 WITH FIELD "id"
DO WRITE IN :11 WITH FIELD "quote"
NOTE A guest is sent to sign in and comes back to /t/:id/reply?quote=:quote.
PLEASE DO ,101 <- "/t/"
DO .71 <- #3
DO :72 <- :10
DO (710) NEXT
DO ,70 <- "/reply"
PLEASE DO .70 <- #6
DO (700) NEXT
DO ,70 <- "?quote="
DO .70 <- #7
DO :72 <- :11
PLEASE DO (770) NEXT
DO (907) NEXT
PLEASE DO (930) NEXT
DO ;49221 <- #12
DO ;49221 SUB #1 <- #2
DO ;49221 SUB #2 <- :10
DO ;49221 SUB #3 <- ;31 SUB #2
PLEASE DO ;49221 SUB #4 <- ;31 SUB #1
DO ;49221 SUB #5 <- ""
DO ;49221 SUB #6 <- ""
DO ;49221 SUB #7 <- #0
DO ;49221 SUB #8 <- ""
PLEASE DO ;49221 SUB #9 <- ""
DO ;49221 SUB #10 <- #0
DO ;49221 SUB #11 <- 'FROM DATABASE #6~#0$#5' BY #3 BY #0 IN BULK
DO ;49221 SUB #12 <- :10
PLEASE DO .5 <- ':11~:11'~#1
DO (2601) NEXT
NOTE Quoting: the post must be live and in this very topic.
DO .5 <- 'FROM DATABASE #4~#0' HAVING #0 :11 HAVING #1 :10 HAVING #6 #0 HOW MANY
DO (2602) NEXT
PLEASE DO ;32 <- 'FROM DATABASE #8~#3$#8' HAVING #0 :11
DO ;49221 SUB #8 <- ;32 SUB #1
DO ;49221 SUB #9 <- ;32 SUB #2
DO (140) NEXT
(2601) DO (980) NEXT
PLEASE DO (140) NEXT
(2602) DO (980) NEXT
DO (802) NEXT

PLEASE NOTE ===== ROUTE 7: POST /t/:id/reply -- POST A REPLY =====
DO COME FROM (16000) WITH ROUTE "POST /t/:id/reply"
DO (900) NEXT
DO (905) NEXT
DO WRITE IN :10 WITH FIELD "id"
PLEASE DO (930) NEXT
DO WRITE IN ,10 WITH FIELD "body"
DO .43 <- .4401
DO WRITE IN :41 WITH FIELD "preview"
DO .47 <- ':41~:41'~#1
PLEASE DO .40 <- #1
DO ,9 <- ""
DO (620) NEXT
DO (690) NEXT
DO (2701) NEXT
NOTE Valid and not a preview: write it and bump the topic.
PLEASE WRITE IN :9 WITH TIME
DO ;33 <- #6
DO ;33 SUB #1 <- :10
DO ;33 SUB #2 <- :100
DO ;33 SUB #3 <- ,10
PLEASE DO ;33 SUB #4 <- :9
DO ;33 SUB #5 <- #0
DO ;33 SUB #6 <- #0
DO STASH ;33 INTO DATABASE #4
DO :11 <- :4400
PLEASE DO 'FROM DATABASE #3~#5' HAVING #0 :10 <- :9
DO ;49221 <- #2
DO ;49221 SUB #1 <- :11
DO ;49221 SUB #2 <- #5
DO READ OUT #2 WITH REDIRECT "to_post.txt"
DO GIVE UP
(2701) PLEASE DO (980) NEXT
DO ;49221 <- #12
DO ;49221 SUB #1 <- #2
DO ;49221 SUB #2 <- :10
DO ;49221 SUB #3 <- ;31 SUB #2
PLEASE DO ;49221 SUB #4 <- ;31 SUB #1
DO ;49221 SUB #5 <- ,10
DO ;49221 SUB #6 <- ,9
DO ;49221 SUB #7 <- .47
DO ;49221 SUB #8 <- ""
PLEASE DO ;49221 SUB #9 <- ""
DO ;49221 SUB #10 <- #0
DO ;49221 SUB #11 <- 'FROM DATABASE #6~#0$#5' BY #3 BY #0 IN BULK
DO ;49221 SUB #12 <- :10
PLEASE DO (140) NEXT

PLEASE NOTE ===== ROUTE 8: GET /new -- THE NEW TOPIC EDITOR =====
DO COME FROM (16000) WITH ROUTE "/new"
DO (900) NEXT
DO WRITE IN :12 WITH FIELD "c"
DO ,101 <- "/new"
PLEASE DO .71 <- #4
DO ,70 <- "?c="
DO .70 <- #3
DO :72 <- :12
DO (770) NEXT
PLEASE DO (907) NEXT
PLEASE DO ;49221 <- #12
DO ;49221 SUB #1 <- #1
DO ;49221 SUB #2 <- #0
DO ;49221 SUB #3 <- :12
DO ;49221 SUB #4 <- ""
PLEASE DO ;49221 SUB #5 <- ""
DO ;49221 SUB #6 <- ""
DO ;49221 SUB #7 <- #0
DO ;49221 SUB #8 <- ""
DO ;49221 SUB #9 <- ""
PLEASE DO ;49221 SUB #10 <- #1
DO ;49221 SUB #11 <- 'FROM DATABASE #6~#0$#5' BY #3 BY #0 IN BULK
DO ;49221 SUB #12 <- #0
DO (140) NEXT

PLEASE NOTE ===== ROUTE 9: POST /new -- CREATE A TOPIC =====
DO COME FROM (140) WITH ROUTE "POST /new"
DO (900) NEXT
DO (905) NEXT
DO WRITE IN :12 WITH FIELD "category"
PLEASE WRITE IN ,11 WITH FIELD "title"
DO .42 <- .4401
DO WRITE IN ,10 WITH FIELD "body"
DO .43 <- .4401
DO WRITE IN :41 WITH FIELD "preview"
PLEASE DO .47 <- ':41~:41'~#1
DO .40 <- #1
DO ,9 <- ""
DO (600) NEXT
DO (610) NEXT
PLEASE DO (620) NEXT
DO (690) NEXT
DO (2901) NEXT
NOTE The topic first, then its opening post.
DO WRITE IN :9 WITH TIME
PLEASE DO ;33 <- #8
DO ;33 SUB #1 <- :12
DO ;33 SUB #2 <- :100
DO ;33 SUB #3 <- ,11
DO ;33 SUB #4 <- :9
PLEASE DO ;33 SUB #5 <- :9
DO ;33 SUB #6 <- #0
DO ;33 SUB #7 <- #0
DO ;33 SUB #8 <- #0
DO STASH ;33 INTO DATABASE #3
PLEASE DO :13 <- :4400
DO ;33 <- #6
DO ;33 SUB #1 <- :13
DO ;33 SUB #2 <- :100
DO ;33 SUB #3 <- ,10
PLEASE DO ;33 SUB #4 <- :9
DO ;33 SUB #5 <- #0
DO ;33 SUB #6 <- #0
DO STASH ;33 INTO DATABASE #4
DO ;49221 <- #2
PLEASE DO ;49221 SUB #1 <- :13
DO ;49221 SUB #2 <- #4
DO READ OUT #2 WITH REDIRECT "to_topic.txt"
DO GIVE UP
(2901) DO (980) NEXT
DO ;49221 <- #12
PLEASE DO ;49221 SUB #1 <- #1
DO ;49221 SUB #2 <- #0
DO ;49221 SUB #3 <- :12
DO ;49221 SUB #4 <- ,11
DO ;49221 SUB #5 <- ,10
PLEASE DO ;49221 SUB #6 <- ,9
DO ;49221 SUB #7 <- .47
DO ;49221 SUB #8 <- ""
DO ;49221 SUB #9 <- ""
DO ;49221 SUB #10 <- #1
PLEASE DO ;49221 SUB #11 <- 'FROM DATABASE #6~#0$#5' BY #3 BY #0 IN BULK
DO ;49221 SUB #12 <- #0
DO (140) NEXT

PLEASE NOTE ===== ROUTE 10: GET /p/:id/edit -- THE EDIT EDITOR =====
DO COME FROM (130) WITH ROUTE "/p/:id/edit"
DO (900) NEXT
PLEASE WRITE IN :10 WITH FIELD "id"
DO ,101 <- "/p/"
DO .71 <- #3
DO :72 <- :10
DO (710) NEXT
PLEASE DO ,70 <- "/edit"
DO .70 <- #5
DO (700) NEXT
DO (907) NEXT
DO (950) NEXT
DO ;49221 <- #12
DO ;49221 SUB #1 <- #3
DO ;49221 SUB #2 <- :10
PLEASE DO ;49221 SUB #3 <- ;34 SUB #6
DO ;49221 SUB #4 <- ;34 SUB #5
DO ;49221 SUB #5 <- ;34 SUB #3
DO ;49221 SUB #6 <- ""
DO ;49221 SUB #7 <- #0
PLEASE DO ;49221 SUB #8 <- ""
DO ;49221 SUB #9 <- ""
DO ;49221 SUB #10 <- .48
DO ;49221 SUB #11 <- 'FROM DATABASE #6~#0$#5' BY #3 BY #0 IN BULK
DO ;49221 SUB #12 <- ;34 SUB #1
PLEASE DO (140) NEXT

PLEASE NOTE ===== ROUTE 11: POST /p/:id/edit -- SAVE AN EDIT =====
NOTE Only the opening post carries the topic title (.48 is 1 then).
NOTE Editing stays allowed in locked topics.
DO COME FROM (140) WITH ROUTE "POST /p/:id/edit"
DO (900) NEXT
PLEASE DO (905) NEXT
DO WRITE IN :10 WITH FIELD "id"
DO (950) NEXT
DO WRITE IN ,11 WITH FIELD "title"
DO .42 <- .4401
PLEASE WRITE IN ,10 WITH FIELD "body"
DO .43 <- .4401
DO WRITE IN :41 WITH FIELD "preview"
DO .47 <- ':41~:41'~#1
DO .40 <- #1
PLEASE DO ,9 <- ""
DO .5 <- .48
DO (3101) NEXT
DO .5 <- '?.48$#1'~'#0$#65535'
DO (3102) NEXT
PLEASE DO (620) NEXT
DO (690) NEXT
DO (3103) NEXT
NOTE Save the body and the edit time, and the title on an opening post.
DO WRITE IN :9 WITH TIME
PLEASE DO ;35 <- #2
DO ;35 SUB #1 <- ,10
DO ;35 SUB #2 <- :9
DO 'FROM DATABASE #4~#3$#5' HAVING #0 :10 <- ;35
DO :14 <- ;34 SUB #1
PLEASE DO .5 <- '?.48$#1'~'#0$#65535'
DO (3104) NEXT
DO ;49221 <- #2
DO ;49221 SUB #1 <- :10
DO ;49221 SUB #2 <- #6
DO READ OUT #2 WITH REDIRECT "to_post.txt"
PLEASE GIVE UP
(3101) DO (980) NEXT
NOTE A reply has no title of its own: show the topic's.
DO ,11 <- ;34 SUB #5
PLEASE RESUME #1
(3102) DO (980) NEXT
DO (610) NEXT
PLEASE RESUME #1
(3104) DO (980) NEXT
DO 'FROM DATABASE #3~#3' HAVING #0 :14 <- ,11
PLEASE RESUME #1
(3103) DO (980) NEXT
DO ;49221 <- #12
DO ;49221 SUB #1 <- #3
PLEASE DO ;49221 SUB #2 <- :10
DO ;49221 SUB #3 <- ;34 SUB #6
DO ;49221 SUB #4 <- ,11
DO ;49221 SUB #5 <- ,10
DO ;49221 SUB #6 <- ,9
PLEASE DO ;49221 SUB #7 <- .47
DO ;49221 SUB #8 <- ""
DO ;49221 SUB #9 <- ""
DO ;49221 SUB #10 <- .48
DO ;49221 SUB #11 <- 'FROM DATABASE #6~#0$#5' BY #3 BY #0 IN BULK
PLEASE DO ;49221 SUB #12 <- ;34 SUB #1
DO (140) NEXT

PLEASE NOTE ===== ROUTE 12: POST /p/:id/delete -- SOFT DELETE =====
NOTE deleted <- 1 by the author, 2 by a moderator; its PLEASEs go too.
DO COME FROM (130) WITH ROUTE "POST /p/:id/delete"
DO (900) NEXT
DO (905) NEXT
PLEASE WRITE IN :10 WITH FIELD "id"
DO (950) NEXT
DO :1 <- ;34 SUB #2
DO :2 <- :100
DO (960) NEXT
PLEASE DO .1 <- .5
DO (1020) NEXT
DO 'FROM DATABASE #4~#6' HAVING #0 :10 <- .1
DO FORGET 'FROM DATABASE #5' HAVING #1 :10
DO ;49221 <- #2
PLEASE DO ;49221 SUB #1 <- :10
DO ;49221 SUB #2 <- #7
DO READ OUT #2 WITH REDIRECT "to_post.txt"
DO GIVE UP

PLEASE NOTE ===== ROUTE 13: POST /p/:id/please -- THE PLEASE TOGGLE =====
DO COME FROM (130) WITH ROUTE "POST /p/:id/please"
DO (900) NEXT
DO (905) NEXT
DO WRITE IN :10 WITH FIELD "id"
PLEASE DO .5 <- 'FROM DATABASE #4~#0' HAVING #0 :10 HAVING #6 #0 HOW MANY
DO (3301) NEXT
DO :1 <- 'FROM DATABASE #4~#2' HAVING #0 :10
DO :2 <- :100
DO (960) NEXT
PLEASE DO (3302) NEXT
NOTE Take back an existing PLEASE; when there was none, add one.
DO .5 <- 'FROM DATABASE #5~#0' HAVING #1 :10 HAVING #2 :100 HOW MANY
DO FORGET 'FROM DATABASE #5' HAVING #1 :10 HAVING #2 :100
DO (3303) NEXT
DO ;49221 <- #2
PLEASE DO ;49221 SUB #1 <- :10
DO ;49221 SUB #2 <- #0
DO READ OUT #2 WITH REDIRECT "to_post.txt"
DO GIVE UP
(3301) DO (980) NEXT
DO (802) NEXT
(3302) PLEASE DO (980) NEXT
DO (813) NEXT
(3303) DO (980) NEXT
DO WRITE IN :9 WITH TIME
DO ;33 <- #3
PLEASE DO ;33 SUB #1 <- :10
DO ;33 SUB #2 <- :100
DO ;33 SUB #3 <- :9
DO STASH ;33 INTO DATABASE #5
PLEASE RESUME #1

PLEASE NOTE ===== ROUTE 14: POST /t/:id/pin -- PIN OR UNPIN (MODERATORS) =====
DO COME FROM (130) WITH ROUTE "POST /t/:id/pin"
DO (900) NEXT
DO (905) NEXT
DO (915) NEXT
PLEASE WRITE IN :10 WITH FIELD "id"
DO (917) NEXT
DO WRITE IN :1 WITH FIELD "pinned"
DO :2 <- #1
DO (960) NEXT
PLEASE DO 'FROM DATABASE #3~#6' HAVING #0 :10 <- '?.5$#1'~'#0$#65535'
NOTE Notice 8 is pinned, 9 unpinned: 8 + .5, .5 being 1 for unpin.
DO .1 <- .5
DO .2 <- #8
PLEASE DO (1000) NEXT
DO :76 <- .3
DO (985) NEXT

PLEASE NOTE ===== ROUTE 15: POST /t/:id/lock -- LOCK OR UNLOCK (MODERATORS) =====
DO COME FROM (130) WITH ROUTE "POST /t/:id/lock"
DO (900) NEXT
DO (905) NEXT
DO (915) NEXT
PLEASE WRITE IN :10 WITH FIELD "id"
DO (917) NEXT
DO WRITE IN :1 WITH FIELD "locked"
DO :2 <- #1
DO (960) NEXT
PLEASE DO 'FROM DATABASE #3~#7' HAVING #0 :10 <- '?.5$#1'~'#0$#65535'
NOTE Notice 10 is locked, 11 unlocked.
DO .1 <- .5
DO .2 <- #10
PLEASE DO (1000) NEXT
DO :76 <- .3
DO (985) NEXT

PLEASE NOTE ===== ROUTE 16: POST /t/:id/move -- MOVE A TOPIC (MODERATORS) =====
DO COME FROM (130) WITH ROUTE "POST /t/:id/move"
DO (900) NEXT
DO (905) NEXT
DO (915) NEXT
PLEASE WRITE IN :10 WITH FIELD "id"
DO (917) NEXT
DO WRITE IN :12 WITH FIELD "category"
DO .5 <- 'FROM DATABASE #2~#0' HAVING #0 :12 HOW MANY
PLEASE DO (3601) NEXT
DO 'FROM DATABASE #3~#1' HAVING #0 :10 <- :12
DO :76 <- #12
DO (985) NEXT
(3601) DO (980) NEXT
PLEASE DO (803) NEXT

PLEASE NOTE ===== ROUTE 17: GET /u/:name -- A PROFILE =====
DO COME FROM (16000) WITH ROUTE "/u/:name"
DO (900) NEXT
DO WRITE IN ,10 WITH FIELD "name"
DO .70 <- .4401
PLEASE DO .5 <- 'FROM DATABASE #9~#0' HAVING #1 ,10 HOW MANY
DO (3701) NEXT
DO ,101 <- "/u/"
DO .71 <- #3
DO ,70 <- ,10
PLEASE DO (700) NEXT
DO ;49221 <- 'FROM DATABASE #9~#0$#1$#2$#3$#4$#5$#6$#7$#8$#9$#10' HAVING #1 ,10
DO :11 <- ;49221 SUB #1
DO STASH ;49221 SUB #12 <- 'FROM DATABASE #10~#0$#2$#5$#6$#9$#10$#12$#8' HAVING #3 :11 HAVING #7 #0 BY #5 BACKWARDS BY #0 BACKWARDS TAKE #10 IN BULK
(170) PLEASE READ OUT #12 WITH TEMPLATE "profile.html"
DO GIVE UP
(3701) DO (980) NEXT
DO (804) NEXT

PLEASE NOTE ===== ROUTE 18: POST /u/:name/role -- PROMOTE OR DEMOTE (ADMIN) =====
NOTE role 2 makes a moderator, role 1 a member again. Admins are fixed.
DO COME FROM (170) WITH ROUTE "POST /u/:name/role"
PLEASE DO (900) NEXT
DO (905) NEXT
DO .5 <- .103
DO (3801) NEXT
DO WRITE IN ,10 WITH FIELD "name"
PLEASE DO .5 <- 'FROM DATABASE #0~#0' HAVING #1 ,10 HOW MANY
DO (3802) NEXT
DO ;36 <- 'FROM DATABASE #0~#0$#3$#1' HAVING #1 ,10
DO :12 <- ;36 SUB #1
DO :1 <- :12
PLEASE DO :2 <- :100
DO (960) NEXT
DO (3803) NEXT
DO :1 <- ;36 SUB #2
DO :2 <- #3
PLEASE DO (960) NEXT
DO (3803) NEXT
NOTE The role must be 1 or 2, so role - 1 must be below 2.
DO WRITE IN :11 WITH FIELD "role"
DO :1 <- :11
PLEASE DO :2 <- #1
DO (1510) NEXT
DO :1 <- :3
DO :2 <- #2
DO (970) NEXT
PLEASE DO (3804) NEXT
DO 'FROM DATABASE #0~#3' HAVING #0 :12 <- :11
DO ;49221 <- #2
DO ;49221 SUB #1 <- ;36 SUB #3
NOTE Notice 13 is promoted (role 2), 14 demoted (role 1): 15 - role.
DO .1 <- #15
PLEASE DO .2 <- :11
DO (1010) NEXT
DO ;49221 SUB #2 <- .3
DO READ OUT #2 WITH REDIRECT "to_user.txt"
PLEASE GIVE UP
(3801) DO (980) NEXT
DO (816) NEXT
(3802) DO (980) NEXT
PLEASE DO (804) NEXT
(3803) DO (980) NEXT
DO (817) NEXT
(3804) DO (980) NEXT
DO (821) NEXT

PLEASE NOTE ===== ROUTE 19: GET /settings -- THE BIO EDITOR =====
DO COME FROM (16000) WITH ROUTE "/settings"
DO (900) NEXT
DO ,101 <- "/settings"
PLEASE DO .71 <- #9
DO (907) NEXT
PLEASE DO ;49221 <- #3
DO ;49221 SUB #1 <- 'FROM DATABASE #0~#5' HAVING #0 :100
DO ;49221 SUB #2 <- ""
DO ;49221 SUB #3 <- #0
DO (180) NEXT

PLEASE NOTE ===== ROUTE 20: POST /settings -- SAVE THE BIO =====
DO COME FROM (180) WITH ROUTE "POST /settings"
DO (900) NEXT
PLEASE DO (905) NEXT
DO WRITE IN ,10 WITH FIELD "bio"
DO .43 <- .4401
DO WRITE IN :41 WITH FIELD "preview"
DO .47 <- ':41~:41'~#1
PLEASE DO .40 <- #1
DO ,9 <- ""
DO (630) NEXT
DO (690) NEXT
DO (4001) NEXT
PLEASE DO 'FROM DATABASE #0~#5' HAVING #0 :100 <- ,10
DO ;49221 <- #2
DO ;49221 SUB #1 <- ,100
DO ;49221 SUB #2 <- #15
DO READ OUT #2 WITH REDIRECT "to_user.txt"
DO GIVE UP
(4001) PLEASE DO (980) NEXT
DO ;49221 <- #3
DO ;49221 SUB #1 <- ,10
DO ;49221 SUB #2 <- ,9
DO ;49221 SUB #3 <- .47
PLEASE DO (180) NEXT

PLEASE NOTE ===== ROUTE 21: GET /search -- TOPICS BY TITLE, POSTS BY BODY =====
DO COME FROM (16000) WITH ROUTE "/search"
DO (900) NEXT
DO .104 <- #3
DO ,101 <- "/search"
DO .71 <- #7
DO WRITE IN ,10 WITH FIELD "q"
PLEASE DO .5 <- .4401
DO (4101) NEXT
DO :20 <- 'FROM DATABASE #10~#0' HAVING #13 SOMEWHERE ,10 HAVING #7 #0 HOW MANY
DO WRITE IN :21 WITH FIELD "page"
DO (910) NEXT
PLEASE DO ;49221 <- #7
DO ;49221 SUB #1 <- ,10
DO ;49221 SUB #2 <- 'FROM DATABASE #10~#0$#1$#2$#4$#5$#6$#9$#10$#12$#8' HAVING #13 SOMEWHERE ,10 HAVING #7 #0 BY #5 BACKWARDS BY #0 BACKWARDS SKIP :26 TAKE #20 IN BULK
DO ;49221 SUB #3 <- :20
DO ;49221 SUB #4 <- :22
PLEASE DO ;49221 SUB #5 <- :23
DO ;49221 SUB #6 <- :24
DO ;49221 SUB #7 <- :25
(190) DO READ OUT #7 WITH TEMPLATE "search.html"
DO GIVE UP
(4101) PLEASE DO (980) NEXT
NOTE No query yet: an empty page with an empty list.
DO ;49221 <- #7
DO ;49221 SUB #1 <- ""
DO ;49221 SUB #2 <- 'FROM DATABASE #10~#0' HAVING #0 #0 IN BULK
DO ;49221 SUB #3 <- #0
PLEASE DO ;49221 SUB #4 <- #1
DO ;49221 SUB #5 <- #1
DO ;49221 SUB #6 <- #0
DO ;49221 SUB #7 <- #0
DO (190) NEXT

PLEASE NOTE ===== ROUTE 22: GET /login -- THE SIGN IN FORM =====
DO COME FROM (16000) WITH ROUTE "/login"
DO (900) NEXT
DO (730) NEXT
PLEASE DO .5 <- ':100~:100'~#1
DO (4201) NEXT
NOTE Already signed in: straight on to the return path, no notice.
DO :76 <- #0
DO (760) NEXT
(4201) DO (980) NEXT
PLEASE DO ;49221 <- #2
DO ;49221 SUB #1 <- ""
DO ;49221 SUB #2 <- ""
DO (150) NEXT

PLEASE NOTE ===== ROUTE 23: POST /login -- SIGN IN =====
NOTE HAVING on the PASSWORD column verifies the hash instead of comparing.
DO COME FROM (150) WITH ROUTE "POST /login"
DO (900) NEXT
DO (730) NEXT
DO WRITE IN ,10 WITH FIELD "name"
PLEASE WRITE IN ,11 WITH FIELD "password"
DO .5 <- 'FROM DATABASE #0~#0' HAVING #1 ,10 HAVING #2 ,11 HOW MANY
DO (4301) NEXT
DO :11 <- 'FROM DATABASE #0~#0' HAVING #1 ,10
PLEASE READ OUT :11 WITH SESSION
DO :76 <- #2
DO (760) NEXT
(4301) DO (980) NEXT
PLEASE DO ;49221 <- #2
DO ;49221 SUB #1 <- "E401 THAT NAME AND PASSWORD DO NOT MINGLE"
DO ;49221 SUB #2 <- ,10
DO (150) NEXT

PLEASE NOTE ===== ROUTE 24: GET /register -- THE REGISTRATION FORM =====
DO COME FROM (16000) WITH ROUTE "/register"
DO (900) NEXT
DO (730) NEXT
PLEASE DO .5 <- ':100~:100'~#1
DO (4401) NEXT
DO :76 <- #0
DO (760) NEXT
(4401) DO (980) NEXT
PLEASE DO ;49221 <- #2
DO ;49221 SUB #1 <- ""
DO ;49221 SUB #2 <- ""
DO (160) NEXT

PLEASE NOTE ===== ROUTE 25: POST /register -- A NEW ACCOUNT =====
NOTE The first account on an empty users table becomes the admin.
DO COME FROM (160) WITH ROUTE "POST /register"
DO (900) NEXT
DO (730) NEXT
PLEASE WRITE IN ,10 WITH FIELD "name"
DO .44 <- .4401
DO WRITE IN ,11 WITH FIELD "password"
DO .45 <- .4401
DO WRITE IN ,12 WITH FIELD "confirm"
PLEASE DO .46 <- .4401
DO .40 <- #1
DO ,9 <- ""
DO (640) NEXT
DO (650) NEXT
PLEASE DO (660) NEXT
DO .5 <- .40
DO (4501) NEXT
NOTE role = 2z+1 where z is 1 when nobody has registered yet: z$#1.
DO .5 <- 'FROM DATABASE #0~#0' HAVING #0 BEYOND #0 HOW MANY
PLEASE DO .5 <- '.5~.5'~#1
DO .5 <- '?.5$#1'~'#0$#65535'
DO WRITE IN :9 WITH TIME
DO ;33 <- #5
DO ;33 SUB #1 <- ,10
PLEASE DO ;33 SUB #2 <- ,11
DO ;33 SUB #3 <- .5$#1
DO ;33 SUB #4 <- :9
DO ;33 SUB #5 <- ""
DO STASH ;33 INTO DATABASE #0
PLEASE READ OUT :4400 WITH SESSION
DO :76 <- #1
DO (760) NEXT
(4501) DO (980) NEXT
PLEASE DO ;49221 <- #2
DO ;49221 SUB #1 <- ,9
DO ;49221 SUB #2 <- ,10
DO (160) NEXT

PLEASE NOTE ===== ROUTE 26: POST /logout -- GIVE UP =====
DO COME FROM (16000) WITH ROUTE "POST /logout"
DO READ OUT #0 WITH SESSION
DO ,101 <- "/"
DO .71 <- #1
DO :76 <- #3
PLEASE DO (760) NEXT

PLEASE NOTE ===== ROUTE 27: * -- EVERYTHING ELSE =====
DO COME FROM (16000) WITH ROUTE "*"
DO (900) NEXT
DO (805) NEXT

PLEASE NOTE ===== RENDER LINES SHARED BY SEVERAL ROUTES =====
NOTE Each form template has exactly one READ OUT line, so its handlers
NOTE NEXT here after filling ;49221 (a re-rendered form answers 200).
(140) PLEASE READ OUT #12 WITH TEMPLATE "compose.html"
DO GIVE UP
(150) DO READ OUT #2 WITH TEMPLATE "login.html"
DO GIVE UP
(160) PLEASE READ OUT #2 WITH TEMPLATE "register.html"
DO GIVE UP
(180) DO READ OUT #3 WITH TEMPLATE "settings.html"
DO GIVE UP
(400) PLEASE READ OUT #2 WITH TEMPLATE "error.html" WITH STATUS #400
DO GIVE UP
(403) DO READ OUT #2 WITH TEMPLATE "error.html" WITH STATUS #403
DO GIVE UP
(404) PLEASE READ OUT #2 WITH TEMPLATE "error.html" WITH STATUS #404
DO GIVE UP

PLEASE NOTE ===== FORM VALIDATORS (600)-(690) =====
NOTE Each validator does nothing once .40 is 0 (an earlier field failed),
NOTE so only the first failure is reported. A failure sets .40 <- #0
PLEASE NOTE and the message in ,9, then returns to the handler.

NOTE (600) the category :12 must exist.
(600) DO .5 <- .40
DO (601) NEXT
PLEASE DO .5 <- 'FROM DATABASE #2~#0' HAVING #0 :12 HOW MANY
DO (602) NEXT
DO RESUME #1
(601) DO (980) NEXT
PLEASE RESUME #2
(602) DO (980) NEXT
DO ,9 <- "E422 PLEASE CHOOSE A CATEGORY"
DO .40 <- #0
PLEASE RESUME #2

NOTE (610) the title ,11 (.42 bytes): not blank, at most 120 bytes.
(610) DO .5 <- .40
DO (611) NEXT
DO ,60 <- ,11
PLEASE DO .60 <- .42
DO (923) NEXT
DO (920) NEXT
DO (612) NEXT
PLEASE DO :1 <- .42
DO :2 <- #121
DO (970) NEXT
DO (613) NEXT
DO RESUME #1
(611) PLEASE DO (980) NEXT
DO RESUME #2
(612) DO (980) NEXT
DO ,9 <- "E422 YOUR TOPIC HAS NO TITLE"
PLEASE DO .40 <- #0
DO RESUME #2
(613) DO (980) NEXT
DO ,9 <- "E422 YOUR TITLE IS LONGER THAN 120 BYTES"
PLEASE DO .40 <- #0
DO RESUME #2

NOTE (620) the body ,10 (.43 bytes): not blank, at most 20 000 bytes.
(620) DO .5 <- .40
DO (621) NEXT
DO ,60 <- ,10
PLEASE DO .60 <- .43
DO (923) NEXT
DO (920) NEXT
DO (622) NEXT
PLEASE DO :1 <- .43
DO :2 <- #20001
DO (970) NEXT
DO (623) NEXT
DO RESUME #1
(621) PLEASE DO (980) NEXT
DO RESUME #2
(622) DO (980) NEXT
DO ,9 <- "E422 YOUR POST IS EMPTY"
PLEASE DO .40 <- #0
DO RESUME #2
(623) DO (980) NEXT
DO ,9 <- "E422 YOUR POST IS LONGER THAN 20 000 BYTES"
PLEASE DO .40 <- #0
DO RESUME #2

NOTE (630) the bio ,10 (.43 bytes): at most 2 000 bytes, may be empty.
(630) DO :1 <- .43
DO :2 <- #2001
PLEASE DO (970) NEXT
DO (631) NEXT
DO RESUME #1
(631) DO (980) NEXT
DO ,9 <- "E422 YOUR BIO IS LONGER THAN 2 000 BYTES"
PLEASE DO .40 <- #0
DO RESUME #2

NOTE (640) the username ,10 (.44 bytes): 3 to 20 bytes, every byte one
NOTE of A-Z a-z 0-9 _ -, and nobody may have it already (any case).
(640) PLEASE DO :1 <- .44
DO :2 <- #3
DO (970) NEXT
DO .5 <- '?.5$#1'~'#0$#65535'
DO (641) NEXT
PLEASE DO :1 <- .44
DO :2 <- #21
DO (970) NEXT
DO (641) NEXT
DO ,60 <- ,10
PLEASE DO .60 <- .44
DO (924) NEXT
DO (920) NEXT
DO .5 <- '?.5$#1'~'#0$#65535'
DO (641) NEXT
PLEASE DO .5 <- 'FROM DATABASE #0~#0' HAVING #1 ,10 HOW MANY
DO .5 <- '?.5$#1'~'#0$#65535'
DO (642) NEXT
DO RESUME #1
(641) DO (980) NEXT
PLEASE DO ,9 <- "E422 USERNAMES ARE 3 TO 20 LETTERS, DIGITS, _ OR -"
DO .40 <- #0
DO RESUME #2
(642) DO (980) NEXT
DO ,9 <- "E422 THAT USERNAME IS TAKEN"
PLEASE DO .40 <- #0
DO RESUME #2

NOTE (650) the password (.45 bytes): at least 8 bytes.
(650) DO .5 <- .40
DO (651) NEXT
DO :1 <- .45
PLEASE DO :2 <- #8
DO (970) NEXT
DO .5 <- '?.5$#1'~'#0$#65535'
DO (652) NEXT
DO RESUME #1
(651) PLEASE DO (980) NEXT
DO RESUME #2
(652) DO (980) NEXT
DO ,9 <- "E422 PASSWORDS NEED AT LEAST 8 BYTES"
PLEASE DO .40 <- #0
DO RESUME #2

NOTE (660) the confirmation ,12 must equal the password ,11 byte for byte.
(660) DO .5 <- .40
DO (661) NEXT
DO :1 <- .45
PLEASE DO :2 <- .46
DO (960) NEXT
DO .5 <- '?.5$#1'~'#0$#65535'
DO (662) NEXT
DO .65 <- #0
PLEASE DO COME FROM (669)
DO .5 <- '?.65$.45'~'#0$#65535'
DO (663) NEXT
DO .1 <- .65
DO (1020) NEXT
PLEASE DO .65 <- .1
DO .66 <- ,11 SUB .65
DO .67 <- ,12 SUB .65
DO .5 <- '?.66$.67'~'#0$#65535'
DO .5 <- '.5~.5'~#1
PLEASE DO .5 <- '?.5$#1'~'#0$#65535'
(669) DO (662) NEXT
(661) DO (980) NEXT
DO RESUME #2
(662) PLEASE DO (980) NEXT
DO ,9 <- "E422 THE PASSWORDS DO NOT MATCH"
DO .40 <- #0
DO RESUME #2
(663) DO (980) NEXT
PLEASE RESUME #2

NOTE (690) save or show? .5 <- 1 to save: the form is valid (.40) and
NOTE the Preview button (.47) was not pressed. 0 re-renders the form.
(690) DO .5 <- '?.47$#1'~'#0$#65535'
PLEASE DO .5 <- .5~.40
DO RESUME #1

PLEASE NOTE ===== ERROR RAISERS (800)-(894): THEY NEVER RETURN =====
(801) DO ,7 <- "NO SUCH TOPIC"
DO (894) NEXT
(802) DO ,7 <- "NO SUCH POST"
PLEASE DO (894) NEXT
(803) DO ,7 <- "NO SUCH CATEGORY"
DO (894) NEXT
(804) DO ,7 <- "NO SUCH USER"
DO (894) NEXT
(805) PLEASE DO ,7 <- "NO SUCH PAGE"
DO (894) NEXT
(811) DO ,7 <- "PLEASE SIGN IN FIRST"
DO (893) NEXT
(812) PLEASE DO ,7 <- "THAT POST IS NOT YOURS"
DO (893) NEXT
(813) DO ,7 <- "YOU CANNOT PLEASE YOURSELF. THAT WOULD BE OVERLY POLITE"
DO (893) NEXT
(814) PLEASE DO ,7 <- "THIS TOPIC IS LOCKED"
DO (893) NEXT
(815) DO ,7 <- "ONLY MODERATORS MAY DO THAT"
DO (893) NEXT
(816) PLEASE DO ,7 <- "ONLY THE ADMIN MAY DO THAT"
DO (893) NEXT
(817) DO ,7 <- "ADMINS CANNOT BE PROMOTED OR DEMOTED"
DO (893) NEXT
(821) PLEASE DO ,7 <- "THAT ROLE DOES NOT EXIST"
DO ;49221 <- #2
DO ;49221 SUB #1 <- #400
DO ;49221 SUB #2 <- ,7
DO (400) NEXT
(893) PLEASE DO ;49221 <- #2
DO ;49221 SUB #1 <- #403
DO ;49221 SUB #2 <- ,7
DO (403) NEXT
(894) DO ;49221 <- #2
PLEASE DO ;49221 SUB #1 <- #404
DO ;49221 SUB #2 <- ,7
DO (404) NEXT

PLEASE NOTE ===== (700)-(799) STRING BUILDING: RETURN PATHS AND NOTICES =====
NOTE ,101 (.71 bytes) is the return path: where a Sign in link, or a guard
NOTE that sends a guest to /login, will bring the reader back to. Pages
PLEASE NOTE build it byte by byte from fixed text and numbers, never from
NOTE what the reader typed; POST /login and /register accept it back only
NOTE through (730). Notices travel as ?done=N, a code into a fixed table
NOTE in _notice.html, and (900) puts it in .105.

NOTE (700) append the .70 bytes of ,70 to ,101.
(700) DO .73 <- #0
DO COME FROM (709)
PLEASE DO .5 <- '?.73$.70'~'#0$#65535'
DO (701) NEXT
DO .1 <- .73
DO (1020) NEXT
DO .73 <- .1
PLEASE DO .1 <- .71
DO (1020) NEXT
DO .71 <- .1
(709) DO STASH ,101 SUB .71 <- ,70 SUB .73
(701) DO (980) NEXT
PLEASE RESUME #2

NOTE (710) append :72 in decimal to ,101. The digits come out backwards,
NOTE so they are stacked from the right of ,74 and copied from there.
(710) DO ,74 <- #10
DO .74 <- #11
PLEASE DO COME FROM (719)
DO :1 <- :72
DO :2 <- #10
DO (1550) NEXT
DO :73 <- :3
PLEASE DO :1 <- :3
DO :2 <- #10
DO (1540) NEXT
DO :1 <- :72
DO :2 <- :3
PLEASE DO (1510) NEXT
DO .1 <- :3
DO .2 <- #48
DO (1000) NEXT
DO .75 <- .3
PLEASE DO .1 <- .74
DO .2 <- #1
DO (1010) NEXT
DO .74 <- .3
DO ,74 SUB .74 <- .75
PLEASE DO :72 <- :73
DO .5 <- ':72~:72'~#1
(719) DO (711) NEXT
(711) DO (980) NEXT
DO COME FROM (718)
PLEASE DO .1 <- .71
DO (1020) NEXT
DO .71 <- .1
DO STASH ,101 SUB .71 <- ,74 SUB .74
PLEASE DO .5 <- '?.74$#10'~'#0$#65535'
DO (712) NEXT
DO .1 <- .74
DO (1020) NEXT
(718) DO .74 <- .1
(712) PLEASE DO (980) NEXT
DO RESUME #3

NOTE (720) append ?done=:76 to ,101, or &done=:76 when it has a query.
(720) DO ,60 <- ,101
DO .60 <- .71
PLEASE DO ,61 <- #256
DO .63 <- #1
DO .64 <- #256
DO (926) NEXT
DO ,61 SUB #64 <- #0
PLEASE DO (920) NEXT
DO ,70 <- "?done="
DO .5 <- '?.5$#1'~'#0$#65535'
DO (721) NEXT
DO .70 <- #6
PLEASE DO (700) NEXT
DO :72 <- :76
DO (710) NEXT
DO RESUME #1
(721) PLEASE DO (980) NEXT
DO ,70 <- "&done="
DO RESUME #1

NOTE (730) THE RETURN PATH FROM A FORM: field next -> ,101 (.71 bytes),
NOTE kept only when it is a same-site path: 1 to 1000 bytes, the first
NOTE a slash, the second neither a slash nor a backslash (so not //host
PLEASE NOTE or /\host), and every byte printable ASCII other than #. CR, LF,
NOTE spaces and https://evil.example all fail. Anything else: empty.
(730) DO WRITE IN ,101 WITH FIELD "next"
DO .71 <- .4401
DO .5 <- .71
PLEASE DO (731) NEXT
DO :1 <- .71
DO :2 <- #1001
DO (970) NEXT
DO (731) NEXT
NOTE A copy with a zero byte on the end, so the second byte always exists.
PLEASE DO ,70 <- ,101
DO .1 <- .71
DO (1020) NEXT
DO STASH ,70 SUB .1 <- #0
DO :1 <- ,70 SUB #1
PLEASE DO :2 <- #47
DO (960) NEXT
DO .5 <- '?.5$#1'~'#0$#65535'
DO (731) NEXT
DO :1 <- ,70 SUB #2
PLEASE DO :2 <- #47
DO (960) NEXT
DO (731) NEXT
DO :1 <- ,70 SUB #2
DO :2 <- #92
PLEASE DO (960) NEXT
DO (731) NEXT
NOTE Bytes 33 to 126 (table entries 34 to 127), except # (entry 36).
DO ,60 <- ,101
DO .60 <- .71
PLEASE DO ,61 <- #256
DO .63 <- #34
DO .64 <- #127
DO (926) NEXT
DO ,61 SUB #36 <- #0
PLEASE DO (920) NEXT
DO .5 <- '?.5$#1'~'#0$#65535'
DO (731) NEXT
DO RESUME #1
(731) DO (980) NEXT
PLEASE DO ,101 <- ""
DO .71 <- #0
DO RESUME #2

NOTE (750) append ?page=:22 to ,101 unless :22 is page 1.
(750) DO :1 <- :22
DO :2 <- #1
PLEASE DO (960) NEXT
DO :1 <- :22
DO :2 <- .5
DO (1540) NEXT
DO :72 <- :3
PLEASE DO ,70 <- "?page="
DO .70 <- #6
DO (770) NEXT
DO RESUME #1

NOTE (760) GO TO THE RETURN PATH: redirect to ,101, or / when it is empty,
NOTE with notice :76 unless that is 0. Never returns.
(760) DO .5 <- .71
PLEASE DO (761) NEXT
DO .5 <- ':76~:76'~#1
DO .5 <- '?.5$#1'~'#0$#65535'
DO (762) NEXT
DO ;49221 <- #1
PLEASE DO ;49221 SUB #1 <- ,101
DO READ OUT #1 WITH REDIRECT "to_next.txt"
DO GIVE UP
(761) DO (980) NEXT
DO ,101 <- "/"
PLEASE DO .71 <- #1
DO RESUME #1
(762) DO (980) NEXT
DO (720) NEXT
PLEASE RESUME #1

NOTE (770) append the ,70 text (.70 bytes) and :72 in decimal to ,101,
NOTE but only when :72 is not zero.
(770) DO .5 <- ':72~:72'~#1
DO .5 <- '?.5$#1'~'#0$#65535'
PLEASE DO (771) NEXT
DO RESUME #1
(771) DO (980) NEXT
DO (700) NEXT
DO (710) NEXT
PLEASE RESUME #2

PLEASE NOTE ===== (900) WHO IS READING? THE SESSION SUBROUTINE =====
NOTE Sets :100 ,100 .101 .102 .103, clears .104, and puts a valid notice
NOTE code from the done field in .105 (0 when absent or out of the table).
NOTE A session naming a vanished user is cleared. The HOW MANY ... BEYOND
NOTE trick gives the flags.
(900) DO WRITE IN :100 WITH SESSION
PLEASE DO .101 <- #0
DO .102 <- #0
DO .103 <- #0
DO .104 <- #0
NOTE Notices are 1 to 15: no bits in the high half and below 16.
DO WRITE IN :75 WITH FIELD "done"
PLEASE DO :1 <- :75~'#65280$#65280'
DO .5 <- ':1~:1'~#1
DO .77 <- '?.5$#1'~'#0$#65535'
DO :1 <- :75
DO :2 <- #16
PLEASE DO (970) NEXT
DO :1 <- :75
DO :2 <- .5~.77
DO (1540) NEXT
DO .105 <- :3
DO .5 <- ':100~:100'~#1
PLEASE DO (901) NEXT
DO .5 <- 'FROM DATABASE #0~#0' HAVING #0 :100 HOW MANY
DO (902) NEXT
DO ;101 <- 'FROM DATABASE #0~#1$#3' HAVING #0 :100
DO ,100 <- ;101 SUB #1
PLEASE DO .101 <- ;101 SUB #2
DO .102 <- 'FROM DATABASE #0~#0' HAVING #0 :100 HAVING #3 BEYOND #1 HOW MANY
DO .103 <- 'FROM DATABASE #0~#0' HAVING #0 :100 HAVING #3 #3 HOW MANY
DO RESUME #1
(901) DO (980) NEXT
PLEASE RESUME #2
(902) DO (980) NEXT
DO :100 <- #0
DO READ OUT #0 WITH SESSION
PLEASE RESUME #2

NOTE (905) POSTs that need an account: E403 PLEASE SIGN IN FIRST.
(905) DO .5 <- ':100~:100'~#1
DO (906) NEXT
PLEASE RESUME #1
(906) DO (980) NEXT
DO (811) NEXT

NOTE (907) pages that need an account send guests to /login instead.
(907) DO .5 <- ':100~:100'~#1
PLEASE DO (908) NEXT
DO RESUME #1
(908) DO (980) NEXT
DO READ OUT #0 WITH REDIRECT "to_login.txt"
PLEASE GIVE UP

PLEASE NOTE (910) PAGINATION: :20 rows, :21 the page asked for (0 means 1).
NOTE Out: :22 page, :23 pages (at least 1), :24 previous (0 on the first),
NOTE :25 next (0 on the last), :26 rows to skip. Past the end: E404.
(910) DO :1 <- :20
DO :2 <- #19
PLEASE DO (1500) NEXT
DO :1 <- :3
DO :2 <- #20
DO (1550) NEXT
DO :23 <- :3
PLEASE DO .5 <- ':23~:23'~#1
DO (912) NEXT
DO :22 <- :21
DO .5 <- ':22~:22'~#1
DO (913) NEXT
PLEASE DO :1 <- :23
DO :2 <- #1
DO (1500) NEXT
DO :1 <- :22
DO :2 <- :3
PLEASE DO (970) NEXT
DO (911) NEXT
DO :1 <- :22
DO :2 <- #1
DO (1510) NEXT
PLEASE DO :24 <- :3
DO :1 <- :24
DO :2 <- #20
DO (1540) NEXT
DO :26 <- :3
PLEASE DO :1 <- :22
DO :2 <- :23
DO (960) NEXT
DO .50 <- .5
DO :1 <- :22
PLEASE DO :2 <- #1
DO (1500) NEXT
DO :1 <- :3
DO :2 <- .50
DO (1540) NEXT
PLEASE DO :25 <- :3
DO RESUME #1
(911) DO (980) NEXT
DO (805) NEXT
(912) PLEASE DO (980) NEXT
DO :23 <- #1
DO RESUME #1
(913) DO (980) NEXT
DO :22 <- #1
PLEASE RESUME #1

NOTE (915) moderator tools: E403 ONLY MODERATORS MAY DO THAT.
(915) DO .5 <- .102
DO (916) NEXT
DO RESUME #1
(916) PLEASE DO (980) NEXT
DO (815) NEXT

NOTE (917) the topic :10 must exist: E404 NO SUCH TOPIC.
(917) DO .5 <- 'FROM DATABASE #3~#0' HAVING #0 :10 HOW MANY
DO (918) NEXT
PLEASE RESUME #1
(918) DO (980) NEXT
DO (801) NEXT

PLEASE NOTE (920) BYTE SCANNER: .5 <- 1 when some byte of ,60 (the first
NOTE .60 bytes) is outside the class table ,61, else 0. The table has
NOTE 256 entries; entry b+1 is 1 when byte b belongs to the class.
(920) DO .61 <- #0
DO COME FROM (929)
DO .5 <- '?.61$.60'~'#0$#65535'
PLEASE DO (921) NEXT
DO .1 <- .61
DO (1020) NEXT
DO .61 <- .1
DO .1 <- ,60 SUB .61
PLEASE DO (1020) NEXT
DO .5 <- ,61 SUB .1
(929) DO (922) NEXT
(921) DO (980) NEXT
PLEASE DO .5 <- #0
DO RESUME #2
(922) DO (980) NEXT
DO .5 <- #1
PLEASE RESUME #2

NOTE (923) the class of blank bytes: tab, LF, CR and space.
(923) DO ,61 <- #256
DO ,61 SUB #10 <- #1
PLEASE DO ,61 SUB #11 <- #1
DO ,61 SUB #14 <- #1
DO ,61 SUB #33 <- #1
DO RESUME #1

NOTE (924) the class of username bytes: - 0-9 A-Z _ a-z (entry = byte + 1).
(924) PLEASE DO ,61 <- #256
DO ,61 SUB #46 <- #1
DO ,61 SUB #96 <- #1
DO .63 <- #49
DO .64 <- #58
PLEASE DO (926) NEXT
DO .63 <- #66
DO .64 <- #91
DO (926) NEXT
DO .63 <- #98
PLEASE DO .64 <- #123
DO (926) NEXT
DO RESUME #1

NOTE (926) ,61 SUB .63 through ,61 SUB .64 <- #1.
(926) DO COME FROM (927)
DO ,61 SUB .63 <- #1
PLEASE DO .5 <- '?.63$.64'~'#0$#65535'
DO (928) NEXT
DO .1 <- .63
DO (1020) NEXT
(927) PLEASE DO .63 <- .1
(928) DO (980) NEXT
DO RESUME #2

NOTE (930) a topic open for replies: :10 must exist (E404) and not be
NOTE locked (E403). ;31 <- [title, category id, locked].
(930) DO .5 <- 'FROM DATABASE #3~#0' HAVING #0 :10 HOW MANY
PLEASE DO (931) NEXT
DO ;31 <- 'FROM DATABASE #7~#4$#1$#10' HAVING #0 :10
DO .5 <- ;31 SUB #3
DO .5 <- '?.5$#1'~'#0$#65535'
DO (932) NEXT
PLEASE RESUME #1
(931) DO (980) NEXT
DO (801) NEXT
(932) DO (980) NEXT
PLEASE DO (814) NEXT

NOTE (950) a post to edit or delete: :10 must be live (E404) and be ours
NOTE or we must moderate (E403). ;34 <- [topic id, author id, body,
NOTE number, topic title, category id, deleted]; .48 <- 1 when it opens
PLEASE NOTE its topic (number 1), so its title may be edited.
(950) DO .5 <- 'FROM DATABASE #4~#0' HAVING #0 :10 HAVING #6 #0 HOW MANY
DO (951) NEXT
DO ;34 <- 'FROM DATABASE #8~#1$#2$#8$#13$#14$#16$#11' HAVING #0 :10
DO :1 <- ;34 SUB #2
PLEASE DO :2 <- :100
DO (960) NEXT
DO .5 <- '?.5$#1'~'#0$#65535'
DO .5 <- 'V.5$.102'~'#0$#65535'
DO (952) NEXT
PLEASE DO :1 <- ;34 SUB #4
DO :2 <- #1
DO (960) NEXT
DO .48 <- '?.5$#1'~'#0$#65535'
DO RESUME #1
(951) PLEASE DO (980) NEXT
DO (802) NEXT
(952) DO (980) NEXT
DO (812) NEXT

PLEASE NOTE (960) .5 <- 1 when :1 and :2 differ, else 0.
(960) DO (1510) NEXT
DO .5 <- ':3~:3'~#1
DO RESUME #1

NOTE (970) .5 <- 1 when :1 is less than :2, else 0 (both below 2^31):
NOTE the sign bit of :1 - :2.
(970) PLEASE DO (1510) NEXT
DO .5 <- :3~'#32768$#0'
DO RESUME #1

NOTE (980) THE BRANCH HELPER, see the header: RESUME #1 when .5 is zero,
NOTE RESUME #2 otherwise. .9 <- nz$(1-nz), which is 1 or 2.
(980) DO .9 <- '.5~.5'~#1
PLEASE DO .8 <- '?.9$#1'~'#0$#65535'
DO .9 <- .9$.8
DO RESUME .9

NOTE (985) after a moderator action: back to the topic :10 with notice :76.
(985) DO ;49221 <- #2
PLEASE DO ;49221 SUB #1 <- :10
DO ;49221 SUB #2 <- :76
DO READ OUT #2 WITH REDIRECT "to_topic.txt"
DO GIVE UP
