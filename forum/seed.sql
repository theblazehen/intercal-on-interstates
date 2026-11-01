-- PLEASE DISCUSS — seed data (PRD feature 10).
--
-- Runs once, right after schema.sql, when the runtime opens an empty
-- database. Times are relative to the moment of seeding, so a fresh forum
-- always looks recently alive. Passwords are plaintext here; the runtime
-- hashes PASSWORD columns on first open. Accounts are listed in
-- forum/README.md. User 1 is the admin (the "first registered account").

-- #0 users (id, name, password, role, joined, bio)
INSERT INTO users (id, name, password, role, joined, bio) VALUES
  (1, 'admin', 'please-give-up', 3, strftime('%s','now')-12096000, 'Keeper of the forum. I wrote the first `COME FROM (16000)` this place ever answered.

If something is broken, it was **probably** the random compiler bug.'),
  (2, 'spark_ears', 'rabbit-ears', 2, strftime('%s','now')-11232000, 'Moderator of *COME FROM Considered Helpful*. Ask me about sparks, rabbit-ears and why your grouping is ambiguous.'),
  (3, 'onespot', 'mingle-select', 1, strftime('%s','now')-8553600, '16 bits ought to be enough for anybody.'),
  (4, 'tailarray', 'interleave!', 1, strftime('%s','now')-6912000, 'Writes INTERCAL for fun and, regrettably, once for money.'),
  (5, 'come_from', 'computed-come-from', 1, strftime('%s','now')-5270400, 'GOTO considered harmful. COME FROM considered helpful.'),
  (6, 'politeness', 'please-please', 1, strftime('%s','now')-3888000, 'Exactly 25% PLEASE, every program, every time.'),
  (7, 'butchered_roman', 'xlii-xlii', 1, strftime('%s','now')-2851200, 'I count in Roman numerals. My post count is in Roman numerals. My rent is not, sadly.'),
  (8, 'e774', 'random-compiler-bug', 1, strftime('%s','now')-1209600, ''),
  (9, 'newbie', 'hello-world', 1, strftime('%s','now')-622800, 'Just found INTERCAL. Send help.'),
  (10, 'cheap_watches', 'not-a-spammer', 1, strftime('%s','now')-691200, '');

-- #1 sections (id, name, position)
INSERT INTO sections (id, name, position) VALUES
  (1, 'The Interstate', 1),
  (2, 'Rest Stops', 2);

-- #2 categories (id, section_id, name, description, color, position)
INSERT INTO categories (id, section_id, name, description, color, position) VALUES
  (1, 1, 'Announcements', 'News from the maintainers. Read-mostly; PLEASE reply politely.', '#b4342d', 1),
  (2, 1, 'COME FROM Considered Helpful', 'Questions, answers and debugging help. Post your error code and the statement it was ON THE WAY TO.', '#0b6e4f', 2),
  (3, 2, 'Show & Tell', 'Things you built with INTERCAL on Interstates: sites, programs, war stories.', '#b7791f', 1),
  (4, 2, 'ABSTAINed (off-topic)', 'Everything else. This category has been ABSTAINED FROM being on topic.', '#6b4fbb', 2);

-- #3 topics (id, category_id, user_id, title, created, bumped, pinned, locked, views)
-- bumped is recomputed from the posts at the end of this file.
INSERT INTO topics (id, category_id, user_id, title, created, bumped, pinned, locked, views) VALUES
  (1, 1, 1, 'Welcome to PLEASE DISCUSS', strftime('%s','now')-8640000, strftime('%s','now')-8640000, 1, 0, 412),
  (2, 1, 1, 'Forum rules: polite, but not too polite', strftime('%s','now')-8636400, strftime('%s','now')-8636400, 1, 1, 388),
  (3, 1, 1, 'INTERCAL on Interstates 1.1 is out (it''s real this time)', strftime('%s','now')-1728000, strftime('%s','now')-1728000, 0, 0, 257),
  (4, 2, 3, 'E079 PROGRAMMER IS INSUFFICIENTLY POLITE, but I said PLEASE!', strftime('%s','now')-1296000, strftime('%s','now')-1296000, 0, 0, 198),
  (5, 2, 4, 'How do I subtract and multiply? (1010 and 1030)', strftime('%s','now')-1036800, strftime('%s','now')-1036800, 0, 0, 143),
  (6, 2, 5, 'ActiveCassette: how do I select several columns at once?', strftime('%s','now')-691200, strftime('%s','now')-691200, 0, 0, 121),
  (7, 2, 6, 'YOU CAN ONLY COME FROM A PAGE. What does it mean?', strftime('%s','now')-259200, strftime('%s','now')-259200, 0, 0, 87),
  (8, 2, 9, 'gross answers 403 YOU CAN''T GET THERE FROM HERE behind nginx', strftime('%s','now')-18000, strftime('%s','now')-18000, 0, 0, 19),
  (9, 3, 7, 'A blog engine in 212 lines of INTERCAL', strftime('%s','now')-2160000, strftime('%s','now')-2160000, 0, 0, 302),
  (10, 3, 5, 'FizzBuzz where every loop is a COME FROM', strftime('%s','now')-864000, strftime('%s','now')-864000, 0, 0, 176),
  (11, 3, 8, 'My error page, in the house style', strftime('%s','now')-518400, strftime('%s','now')-518400, 0, 0, 98),
  (12, 4, 8, 'What''s your favourite INTERCAL error message?', strftime('%s','now')-777600, strftime('%s','now')-777600, 0, 0, 644),
  (13, 4, 6, 'Is it rude to end every message with PLEASE GIVE UP?', strftime('%s','now')-345600, strftime('%s','now')-345600, 0, 0, 133),
  (14, 4, 3, 'Post your terminal setup (punched cards welcome)', strftime('%s','now')-86400, strftime('%s','now')-86400, 0, 0, 41);

-- #4 posts (id, topic_id, user_id, body, created, edited, deleted)
INSERT INTO posts (id, topic_id, user_id, body, created, edited, deleted) VALUES
  (1, 1, 1, 'Welcome to **PLEASE DISCUSS**, the community forum for INTERCAL on Interstates, and as far as we know the only forum written in INTERCAL.

A few things work differently here:

- Liking a post is called **PLEASE**. You can''t PLEASE your own posts; that would be superfluously polite.
- Signing out is **GIVE UP**. It''s in the top right.
- Counts are in Roman numerals. Hover one if you need the boring version.
- Every page you can reach was reached via `COME FROM`. If a form answers *YOU CAN''T GET THERE FROM HERE*, you came from somewhere you shouldn''t have.

Stuck? Ask in [COME FROM Considered Helpful](/c/2). Built something? Show us in [Show & Tell](/c/3).

PLEASE DO enjoy yourself.', strftime('%s','now')-8640000, 0, 0),
  (2, 1, 2, 'Thrilled to finally have somewhere to put questions that isn''t a mailing list from 1990. I''ll be moderating the help category; mention @spark_ears if a thread needs a lock or a move.', strftime('%s','now')-8625600, 0, 0),
  (3, 1, 3, 'Is the forum itself really INTERCAL? The page rendered faster than my blog does. I don''t believe it.', strftime('%s','now')-8474400, 0, 0),
  (4, 1, 1, '[quote="onespot"]Is the forum itself really INTERCAL?[/quote]

It is. Every route is a `COME FROM`, every query is a `FROM DATABASE`, and the pagination maths goes through `(1030)` and `(1040)` in the system library. The speed is mostly SQLite being good at its job.', strftime('%s','now')-8467200, 0, 0),
  (5, 2, 1, '1. **Be polite.** Somewhere between one-fifth and one-third of what you say should be PLEASE. Fewer and the moderators will call you insufficiently polite (E079). More and you are overly polite (E099).
2. **Post code in fences.** Three backticks, then your program. Line labels make it easier for people to tell you which statement you were ON THE WAY TO.
3. **Search before asking.** Most error codes have been explained at least twice.
4. **No spam.** Spam is ABSTAINED FROM on sight.
5. **Be kind to beginners.** Everybody''s first INTERCAL program says E079.

This topic is locked. If you disagree with a rule, start a thread in ABSTAINed.', strftime('%s','now')-8636400, 0, 0),
  (6, 3, 1, 'Thirteen years after the April Fools'' announcement, **INTERCAL on Interstates 1.1** exists and does everything the 2013 site claimed:

- routing with `COME FROM (16000) WITH ROUTE "/blog/post/:id"`, and static link checking at compile time
- templates with double grumpies, `{{,144}}` and friends
- ActiveCassette on SQLite: `''FROM DATABASE #1~#3$#4$#5'' HAVING #0 ,3`
- **gross**, the General READ OUT Server System, on port 10585

The whole of Hello World:

```
DO COME FROM (16000)
PLEASE READ OUT #0 WITH TEMPLATE "hello.html"
GIVE UP
```

Report bugs in [COME FROM Considered Helpful](/c/2).', strftime('%s','now')-1728000, 0, 0),
  (7, 3, 4, 'Built the hello world in about a minute. Then spent an hour working out why `gross` wouldn''t start: something else was already on 10585. My fault, not yours.', strftime('%s','now')-1663200, 0, 0),
  (8, 3, 5, 'The static link checking is genuinely useful. It caught a form posting to a route I''d renamed. In INTERCAL. I need to lie down.', strftime('%s','now')-1648800, 0, 0),
  (9, 3, 7, 'Topic and post counts in Roman numerals is the correct design decision and I will not be taking questions.', strftime('%s','now')-1555200, 0, 0),
  (10, 4, 3, 'My first route handler:

```
DO COME FROM (16000) WITH ROUTE "/hello/:name"
DO ,1 <- ;4400 SUB #1
DO STASH ;49221 SUB #1 <- ,1
DO READ OUT #1 WITH TEMPLATE "hello.html"
PLEASE GIVE UP
NOTE greet the visitor
```

and ick says

```
ICL079I PROGRAMMER IS INSUFFICIENTLY POLITE
```

There''s a PLEASE right there. What am I missing?', strftime('%s','now')-1296000, 0, 0),
  (11, 4, 2, 'The rule counts statements, not good intentions. You have **six** statements (the `NOTE` counts too) and **one** PLEASE. At least one in five must be polite, so you need two.

Make the READ OUT polite:

```
PLEASE READ OUT #1 WITH TEMPLATE "hello.html"
```

That puts you at two in six, exactly one-third, which is the most you''re allowed. One more PLEASE and it''s E099 PROGRAMMER IS OVERLY POLITE.', strftime('%s','now')-1293600, 0, 0),
  (12, 4, 3, 'That worked. I have never felt so judged by a compiler. PLEASE accept my thanks.', strftime('%s','now')-1288800, 0, 0),
  (13, 4, 6, 'Pro tip: I keep every program at exactly 25% PLEASE. Polite enough to compile, not so polite it looks like I want something.', strftime('%s','now')-1209600, 0, 0),
  (14, 5, 4, 'I need the offset for pagination, `(page - 1) * 20`. The system library has `(1000)`, `(1009)`, `(1010)`, `(1030)`... which ones do I want, and where do the arguments go?', strftime('%s','now')-1036800, 0, 0),
  (15, 5, 5, 'Arguments go in `.1` and `.2`, the result comes back in `.3`:

- `(1000)` is `.3 <- .1 plus .2`
- `(1010)` is `.3 <- .1 minus .2`
- `(1030)` is `.3 <- .1 times .2`
- `(1040)` is `.3 <- .1 divided by .2`

So:

```
DO .1 <- .7
DO .2 <- #1
DO (1010) NEXT
DO .1 <- .3
PLEASE DO .2 <- #20
DO (1030) NEXT
```

and `.3` is your offset. This forum does exactly that for `?page=N`.', strftime('%s','now')-1026000, 0, 0),
  (16, 5, 4, 'Perfect, thank you. Marking this as solved by PLEASEing it, since we don''t have a solved button. (Feature request?)', strftime('%s','now')-1022400, 0, 0),
  (17, 6, 5, '`''FROM DATABASE #3~#3'' HAVING #0 .1` gives me a topic''s title. How do I get title, author and date without three queries?', strftime('%s','now')-691200, 0, 0),
  (18, 6, 2, 'Mingle the column numbers:

```
DO STASH ;49221 <- ''FROM DATABASE #7~#4$#6$#7'' HAVING #0 .1
```

Several columns come back as a LIST, which fills `;49221` directly. Add `IN BULK` to get every matching row, and `BY #8 BACKWARDS TAKE #20` to page through them.

Joins live in views, so check what `schema.sql` already gives you before writing your own.', strftime('%s','now')-687600, strftime('%s','now')-688200, 0),
  (19, 6, 5, 'So the `$` in the selector doesn''t actually mingle. That''s the most INTERCAL thing I''ve read today.', strftime('%s','now')-684000, 0, 0),
  (20, 6, 8, 'nvm, wrong thread', strftime('%s','now')-604800, 0, 1),
  (21, 7, 6, 'I wrote

```
(5) DO COME FROM (12) WITH ROUTE "POST /comment"
```

where `(12)` is a plain assignment, and ick refuses to compile it. Why can''t I come from wherever I like?', strftime('%s','now')-259200, 0, 0),
  (22, 7, 2, 'A route''s origin label has to be **16000** (anyone may arrive) or the label of a line that does `READ OUT ... WITH TEMPLATE`. A request to that route can then only have come from the page that line rendered. At run time the framework checks the `Origin`/`Referer` header too, so it doubles as CSRF protection.

Put the label on the line that renders the page with the comment form and you''re done.', strftime('%s','now')-255600, 0, 0),
  (23, 7, 6, 'Fixed. It also told me my comment form linked to `/coment`. Typos: ABSTAINED FROM.', strftime('%s','now')-244800, 0, 0),
  (24, 8, 9, 'Running gross behind nginx and every POST gets a 403. GETs are fine. I''m proxying `/` to `127.0.0.1:10585`. Any idea?', strftime('%s','now')-18000, 0, 0),
  (25, 8, 2, 'Your proxy is rewriting the `Host` header, so the `Origin` your browser sends no longer names the same host. Add

```
proxy_set_header Host $host;
```

and the origin check will pass. @newbie, welcome aboard!', strftime('%s','now')-14400, 0, 0),
  (26, 9, 7, 'Posts, tags, an Atom feed and an admin page, all on INTERCAL on Interstates. The feed template is `feed.xml`, so it''s served as `application/xml` without me doing anything.

The part I''m proudest of: dates on the front page use `AGO`, but the archive groups by year `IN ROMAN`, so this year reads as MMXXVI, as nature intended.

PLEASE be gentle; it''s my first real program.', strftime('%s','now')-2160000, 0, 0),
  (27, 9, 4, '212 lines including syslib, or not including? Either way, impressive.', strftime('%s','now')-2138400, 0, 0),
  (28, 9, 7, 'Not including. With syslib it''s... more. Let''s not.', strftime('%s','now')-2134800, 0, 0),
  (29, 9, 3, 'Stealing the Roman archive idea immediately.', strftime('%s','now')-2073600, 0, 0),
  (30, 10, 5, 'Every loop is a `COME FROM`; the only `NEXT`s are into the system library for arithmetic. It prints 1 to 100 as Roman numerals, of course, so the FizzBuzzes land on XV, XXX, XLV...

```
(10)  DO .7 <- #0
(20)  DO COME FROM (90)
      DO .1 <- .7
      DO (1020) NEXT
      DO .7 <- .1
      PLEASE ABSTAIN FROM (40)
```

The full listing is 140 lines and every one of them hurts.', strftime('%s','now')-864000, 0, 0),
  (31, 10, 8, 'First run: ICL774I RANDOM COMPILER BUG. Second run: a perfect FizzBuzz. 10/10, very authentic.', strftime('%s','now')-820800, 0, 0),
  (32, 10, 7, 'XV, XXX, XLV. The FizzBuzzes look like sports scores.', strftime('%s','now')-813600, 0, 0),
  (33, 11, 8, 'Custom 404 for my site:

```
<h1>E404 NO SUCH PAGE</h1>
<p>PLEASE CORRECT SOURCE AND RESUBNIT</p>
```

The catch-all is `COME FROM (16000) WITH ROUTE "*"`, declared last. Visitors have described it as "hostile", which I take as a compliment.', strftime('%s','now')-518400, 0, 0),
  (34, 11, 6, 'RESUBNIT is a typo, right?', strftime('%s','now')-511200, 0, 0),
  (35, 11, 2, '[quote="politeness"]RESUBNIT is a typo, right?[/quote]

It''s a typo from 1972 and it is load-bearing. Do not touch it.', strftime('%s','now')-507600, 0, 0),
  (36, 12, 8, 'Mine is obviously **ICL774I RANDOM COMPILER BUG**. It''s not a bug in your program and it''s not exactly a bug in the compiler: it''s a *feature* of the compiler that it has bugs.', strftime('%s','now')-777600, 0, 0),
  (37, 12, 3, 'ICL079I PROGRAMMER IS INSUFFICIENTLY POLITE, for personal reasons.', strftime('%s','now')-752400, 0, 0),
  (38, 12, 6, 'ICL099I PROGRAMMER IS OVERLY POLITE. I have been both. Neither felt good.', strftime('%s','now')-727200, 0, 0),
  (39, 12, 4, 'ICL123I PROGRAM HAS DISAPPEARED INTO THE BLACK LAGOON. That''s what you get for NEXTing 80 levels deep.', strftime('%s','now')-702000, 0, 0),
  (40, 12, 5, 'ICL555I FLOW DIAGRAM IS EXCESSIVELY CONNECTED. Two COME FROMs, one label. I''ve done it more than once.', strftime('%s','now')-676800, 0, 0),
  (41, 12, 7, 'ICL533I YOU WANT MAYBE WE SHOULD IMPLEMENT 64-BIT VARIABLES?', strftime('%s','now')-651600, 0, 0),
  (42, 12, 2, 'ICL000I followed by your own statement echoed back at you. It''s the compiler saying *I''m not angry, just disappointed*.', strftime('%s','now')-626400, 0, 0),
  (43, 12, 1, 'ICL129I PROGRAM HAS GOTTEN LOST. Relatable.', strftime('%s','now')-601200, 0, 0),
  (44, 12, 9, 'I''ve been here a week and I''ve already seen E079 eleven times.', strftime('%s','now')-576000, 0, 0),
  (45, 12, 3, '@newbie it gets better. You start seeing E099 instead.', strftime('%s','now')-550800, 0, 0),
  (46, 12, 8, 'ICL436I THROW STICK BEFORE RETRIEVING! STASH and RETRIEVE in the wrong order.', strftime('%s','now')-525600, 0, 0),
  (47, 12, 4, 'ICL241I VARIABLES MAY NOT BE STORED IN WEST HYPERSPACE. Subscript out of range, poetically.', strftime('%s','now')-511200, 0, 0),
  (48, 12, 6, 'ICL275I DON''T BYTE OFF MORE THAN YOU CAN CHEW. Too big for a 16-bit variable.', strftime('%s','now')-496800, 0, 0),
  (49, 12, 5, 'And the Interstates one: **YOU CAN''T GET THERE FROM HERE**. Every CSRF failure should be that polite.', strftime('%s','now')-482400, 0, 0),
  (50, 12, 7, 'ICL200I NOTHING VENTURED, NOTHING GAINED. I don''t remember what triggers it. I just like it.', strftime('%s','now')-468000, 0, 0),
  (51, 12, 2, 'ICL017I DO YOU EXPECT ME TO FIGURE THIS OUT? The classic.', strftime('%s','now')-453600, 0, 0),
  (52, 12, 1, 'Pinning this would be off-topic for Announcements, but I''m tempted.', strftime('%s','now')-439200, 0, 0),
  (53, 12, 9, 'Does anyone have a printed list? I''d put it on the wall.', strftime('%s','now')-424800, 0, 0),
  (54, 12, 4, '`info ick` has the full table. Print it landscape.', strftime('%s','now')-410400, 0, 0),
  (55, 12, 3, 'ICL632I THE NEXT STACK RUPTURES. ALL DIE. OH, THE EMBARRASSMENT!', strftime('%s','now')-396000, 0, 0),
  (56, 12, 6, 'ICL621I ERROR TYPE 621 ENCOUNTERED. My favourite for being so honest about being no help at all.', strftime('%s','now')-381600, 0, 0),
  (57, 12, 8, 'We''re on page II of this thread now. Pagination in Roman numerals. Beautiful.', strftime('%s','now')-367200, 0, 0),
  (58, 12, 7, 'XXIII posts and counting.', strftime('%s','now')-352800, 0, 0),
  (59, 13, 6, 'Asking for a friend who signs every email that way.', strftime('%s','now')-345600, 0, 0),
  (60, 13, 7, 'Only if the friend''s emails are less than one-fifth PLEASE.', strftime('%s','now')-338400, 0, 0),
  (61, 13, 10, 'Best replica watches!!! cheap prices, visit my site', strftime('%s','now')-302400, 0, 2),
  (62, 13, 9, 'I thought it was a sign-off, like *cheers*. I have been using it at work.

*Edit: I have stopped using it at work.*', strftime('%s','now')-280800, strftime('%s','now')-259200, 0),
  (63, 14, 3, 'Mine: a 40-column terminal emulator in amber, because if I''m going to write INTERCAL I''m going to suffer properly. Yours?', strftime('%s','now')-86400, 0, 0),
  (64, 14, 4, 'Green on black, 80 columns, and a font with a proper `¢` because I still mingle with it out of habit.', strftime('%s','now')-68400, 0, 0),
  (65, 14, 8, 'A literal box of punched cards from my grandmother''s office. They don''t run anything. They''re there for morale.', strftime('%s','now')-7200, 0, 0);

-- #5 pleases (id, post_id, user_id, created)
INSERT INTO pleases (id, post_id, user_id, created) VALUES
  (1, 1, 2, strftime('%s','now')-8629089),
  (2, 1, 3, strftime('%s','now')-8548357),
  (3, 1, 6, strftime('%s','now')-3874763),
  (4, 1, 7, strftime('%s','now')-2849318),
  (5, 1, 8, strftime('%s','now')-1206927),
  (6, 1, 9, strftime('%s','now')-604941),
  (7, 4, 3, strftime('%s','now')-8463816),
  (8, 4, 6, strftime('%s','now')-3875718),
  (9, 4, 9, strftime('%s','now')-603404),
  (10, 5, 4, strftime('%s','now')-6909800),
  (11, 5, 5, strftime('%s','now')-5253473),
  (12, 5, 6, strftime('%s','now')-3880665),
  (13, 5, 8, strftime('%s','now')-1208072),
  (14, 5, 9, strftime('%s','now')-619684),
  (15, 6, 3, strftime('%s','now')-1713491),
  (16, 6, 4, strftime('%s','now')-1713998),
  (17, 6, 5, strftime('%s','now')-1725411),
  (18, 8, 3, strftime('%s','now')-1640614),
  (19, 8, 4, strftime('%s','now')-1645528),
  (20, 8, 6, strftime('%s','now')-1630444),
  (21, 8, 7, strftime('%s','now')-1634590),
  (22, 9, 3, strftime('%s','now')-1552964),
  (23, 9, 5, strftime('%s','now')-1536372),
  (24, 9, 6, strftime('%s','now')-1550844),
  (25, 9, 8, strftime('%s','now')-1201985),
  (26, 9, 9, strftime('%s','now')-601836),
  (27, 11, 1, strftime('%s','now')-1272741),
  (28, 11, 3, strftime('%s','now')-1274197),
  (29, 11, 5, strftime('%s','now')-1291273),
  (30, 11, 8, strftime('%s','now')-1190390),
  (31, 12, 1, strftime('%s','now')-1269313),
  (32, 12, 9, strftime('%s','now')-609502),
  (33, 14, 2, strftime('%s','now')-1034876),
  (34, 14, 3, strftime('%s','now')-1029256),
  (35, 14, 6, strftime('%s','now')-1034974),
  (36, 16, 2, strftime('%s','now')-1003860),
  (37, 16, 3, strftime('%s','now')-1017737),
  (38, 16, 5, strftime('%s','now')-1012611),
  (39, 16, 7, strftime('%s','now')-1008366),
  (40, 17, 3, strftime('%s','now')-686174),
  (41, 17, 9, strftime('%s','now')-604783),
  (42, 18, 3, strftime('%s','now')-683441),
  (43, 18, 4, strftime('%s','now')-668593),
  (44, 18, 5, strftime('%s','now')-677192),
  (45, 21, 4, strftime('%s','now')-240542),
  (46, 21, 5, strftime('%s','now')-252978),
  (47, 24, 2, strftime('%s','now')-14324),
  (48, 24, 8, strftime('%s','now')-60),
  (49, 28, 8, strftime('%s','now')-1190583),
  (50, 28, 9, strftime('%s','now')-601565),
  (51, 31, 2, strftime('%s','now')-814344),
  (52, 31, 3, strftime('%s','now')-808298),
  (53, 31, 7, strftime('%s','now')-817308),
  (54, 34, 2, strftime('%s','now')-492952),
  (55, 34, 3, strftime('%s','now')-508843),
  (56, 34, 5, strftime('%s','now')-492407),
  (57, 34, 8, strftime('%s','now')-508947),
  (58, 34, 9, strftime('%s','now')-490617),
  (59, 36, 2, strftime('%s','now')-770552),
  (60, 36, 5, strftime('%s','now')-761034),
  (61, 36, 7, strftime('%s','now')-759877),
  (62, 36, 9, strftime('%s','now')-608489),
  (63, 37, 6, strftime('%s','now')-741807),
  (64, 37, 8, strftime('%s','now')-736844),
  (65, 40, 1, strftime('%s','now')-657313),
  (66, 40, 2, strftime('%s','now')-661651),
  (67, 40, 7, strftime('%s','now')-664652),
  (68, 42, 1, strftime('%s','now')-616278),
  (69, 42, 5, strftime('%s','now')-617960),
  (70, 42, 8, strftime('%s','now')-620210),
  (71, 44, 1, strftime('%s','now')-567702),
  (72, 44, 3, strftime('%s','now')-573018),
  (73, 44, 4, strftime('%s','now')-556878),
  (74, 44, 5, strftime('%s','now')-565862),
  (75, 44, 8, strftime('%s','now')-558491),
  (76, 51, 6, strftime('%s','now')-437077),
  (77, 51, 7, strftime('%s','now')-442045),
  (78, 53, 3, strftime('%s','now')-409793),
  (79, 53, 4, strftime('%s','now')-415065),
  (80, 53, 6, strftime('%s','now')-404546),
  (81, 57, 1, strftime('%s','now')-364502),
  (82, 58, 1, strftime('%s','now')-348632),
  (83, 58, 4, strftime('%s','now')-335725),
  (84, 58, 6, strftime('%s','now')-338799),
  (85, 59, 3, strftime('%s','now')-339895),
  (86, 59, 5, strftime('%s','now')-334092),
  (87, 63, 1, strftime('%s','now')-81120),
  (88, 63, 4, strftime('%s','now')-70078),
  (89, 63, 8, strftime('%s','now')-72282),
  (90, 64, 6, strftime('%s','now')-66816),
  (91, 64, 9, strftime('%s','now')-65557);

-- Last activity = newest live post.
UPDATE topics SET bumped = COALESCE(
  (SELECT MAX(created) FROM posts WHERE topic_id = topics.id AND deleted = 0), created);
