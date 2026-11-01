# PLEASE DISCUSS — route and data contract for `forum.i`

This is everything the author of `forum.i` needs. It fixes every route, origin label,
database number, template parameter order, validation message and redirect. The language
rules are in `SPEC.md` (§2 for routing, fields, sessions and ActiveCassette, §4 for templates).
Where this file says "must", forum.i must do exactly that or a template or static link check
will break.

Files next to `forum.i`: `schema.sql`, `seed.sql`, `templates/`, `public/` (served by gross at
`/public/...`). Build: `ick -s -b forum.i`, run: `gross -d public ./forum`, both from this
directory (the program's directory is its `IOI_APP_DIR` by default).

---

## 1. Conventions

### 1.1 READ OUT labels (route origins)

Each page template is rendered from exactly **one** labelled `READ OUT` line. Routes whose
forms or links appear only on that page COME FROM that label; everything else COMEs FROM 16000.
Use these labels:

| Label | Statement | Template |
|---|---|---|
| (100) | `READ OUT #2 WITH TEMPLATE "index.html"` | board index |
| (110) | `READ OUT #5 WITH TEMPLATE "latest.html"` | latest |
| (120) | `READ OUT #11 WITH TEMPLATE "category.html"` | category |
| (130) | `READ OUT #16 WITH TEMPLATE "topic.html"` | topic |
| (140) | `READ OUT #12 WITH TEMPLATE "compose.html"` | new topic / reply / edit |
| (150) | `READ OUT #2 WITH TEMPLATE "login.html"` | sign in |
| (160) | `READ OUT #2 WITH TEMPLATE "register.html"` | register |
| (170) | `READ OUT #12 WITH TEMPLATE "profile.html"` | profile |
| (180) | `READ OUT #3 WITH TEMPLATE "settings.html"` | bio editor |
| (190) | `READ OUT #7 WITH TEMPLATE "search.html"` | search |
| (400) | `READ OUT #2 WITH TEMPLATE "error.html" WITH STATUS #400` | error |
| (403) | `READ OUT #2 WITH TEMPLATE "error.html" WITH STATUS #403` | error |
| (404) | `READ OUT #2 WITH TEMPLATE "error.html" WITH STATUS #404` | error |

Reach a render line with `DO (130) NEXT` (or by falling into it) after filling `;49221`; follow
each render line with `PLEASE GIVE UP`. Route COME FROMs never fire from ordinary control
flow, so executing line (150) does not jump into `POST /login`.

`error.html` links only to 16000-origin routes, which is why three lines may render it.
Redirect templates (§1.6) contain no links and may be rendered from any line.

Validation failures re-render the form from its single line, so they answer **200** with the
error message in the page (a form template cannot be rendered from a second line with another
status: its non-16000 form actions would fail the static link check there).

### 1.2 Program variables read by templates

A shared subroutine, called first thing by every handler (`DO (900) NEXT`), sets:

| Variable | Meaning |
|---|---|
| `:100` | signed-in user id, `#0` for a guest |
| `,100` | signed-in user name (never dimensioned / empty for a guest) |
| `.101` | role: 0 guest, 1 member, 2 moderator, 3 admin |
| `.102` | `#1` if moderator or admin, else `#0` |
| `.103` | `#1` if admin, else `#0` |
| `.104` | active nav tab: `#1` board index, `#2` latest, `#3` search, `#0` otherwise. The Latest page **must** set `#2`: `_topic_row.html` shows category badges only then |
| `;100` | topic page only: `;100 SUB k` is `#1` when the signed-in user PLEASEd post row k of the page, else `#0`. Always dimension it to at least 1 (`;100 <- #20` works) and zero it, even for guests |
| `,101` | the **return path** (§1.10): where Sign in / Register links, and the guard that sends a guest to `/login`, will bring the reader back to. Built by each page from fixed text and numbers; on `/login` and `/register` it is the validated `next` field. May be empty |
| `.105` | the **notice** code (§1.10), 1–15, from the `done` field; `#0` for none |

Session subroutine outline:

```
(900) WRITE IN :100 WITH SESSION
      .101 <- #0  .102 <- #0  .103 <- #0  .104 <- #0
      if :100 = 0 → RESUME #1
      ;101 <- 'FROM DATABASE #0~#1$#3' HAVING #0 :100        (LIST [name, role], empty if gone)
      if HOW MANY of that = 0 → :100 <- #0, READ OUT #0 WITH SESSION, RESUME #1
      ,100 <- ;101 SUB #1      .101 <- ;101 SUB #2
      .102 <- 'FROM DATABASE #0~#0' HAVING #0 :100 HAVING #3 BEYOND #1 HOW MANY
      .103 <- 'FROM DATABASE #0~#0' HAVING #0 :100 HAVING #3 #3 HOW MANY
```

The `HOW MANY … BEYOND` trick gives 1/0 flags without comparisons in INTERCAL.

### 1.3 Variables the program must not keep state in

Every `READ OUT … WITH TEMPLATE` extracts `;49221` into `,144` `,143` … and `;144` `;143` …,
and template loops overwrite `,144` downwards and `.144`. Treat `,120`–`,144`, `;120`–`;144` and
`.144` as scratch that a READ OUT destroys. `,100`–`,104`, `:100`, `.101`–`.105`, `;100` are the
template globals above. Fill `;49221` fresh before each READ OUT (whole-array assignment from
a LIST, then `STASH ;49221 SUB #k <- …` for the rest; STASH-assign extends the array).

### 1.4 Values in templates

- A number in `;49221` (or in a loop row) arrives in its tail array as **decimal digits**, so
  templates compare flags with `IS #0` / `IS #1` (the bytes `"0"` are truthy), and print counts
  with `IN ROMAN` and `title="{{,n}}"` (arabic tooltip).
- Text arrives as bytes, NULL as empty bytes. All view columns are non-NULL.
- Times are Unix seconds; templates print them with `AGO`.
- A LIST in `;49221` lands in `;145−k` and templates loop over it with `{{DO ;n NEXT}}`.
  Always pass a LIST where a list is expected; for "nothing" pass an empty LIST from a query
  that matches no rows (e.g. `'FROM DATABASE #10~#0' HAVING #0 #0 IN BULK`).

### 1.5 Reading input

- Route ids: `WRITE IN :1 WITH FIELD "id"` (route params are found first; number, 0 if not a
  number). Bind numbers, not bytes, to view columns.
- `:name` in `/u/:name`: `WRITE IN ,1 WITH FIELD "name"`.
- Page numbers: `WRITE IN .5 WITH FIELD "page"`, 0 when absent → treat as 1.
- Text fields: `WRITE IN ,n WITH FIELD "body"`; `.4401` is the byte length right after.
- `preview` is present (value 1) only when the Preview button was pressed:
  `WRITE IN .6 WITH FIELD "preview"`.
- Time: `WRITE IN :9 WITH TIME`.

### 1.6 Redirects (`READ OUT #n WITH REDIRECT "file"`, 303)

| Template | Location | `;49221` |
|---|---|---|
| `to_login.txt` | `/login?next={{,101}}` | none (reads the return path `,101`) |
| `to_next.txt` | `{{,144}}` — the whole Location, built and validated in forum.i (§1.10) | [path] |
| `to_topic.txt` | `/t/{{,144}}?done={{,143}}` | [topic id, notice] |
| `to_post.txt` | `/p/{{,144}}`, plus `?done={{,143}}` unless the notice is `#0` (permalink; GET /p/:id then lands on the right page) | [post id, notice] |
| `to_topic_page.txt` | `/t/{{,144}}?page={{,143}}`, plus `&done={{,141}}` unless `#0`, then `#p{{,142}}` | [topic id, page, post id, notice] |
| `to_user.txt` | `/u/{{,144}}?done={{,143}}` | [user name, notice] |

`to_next.txt` has no static text, so the compiler's link check cannot vouch for it; that is
why forum.i only ever puts a path through it that it built itself or that passed (730).

### 1.7 Pagination (20 per page, arithmetic via syslib)

```
n      = HOW MANY rows                       (count query in each route)
pages  = (n + 19) / 20, but at least 1       (1000) add, (1040) divide
page   = field "page"; 0 → 1
page > pages → error 404 "NO SUCH PAGE"
skip   = (page − 1) × 20                     (1010) subtract, (1030) multiply
prev   = page − 1  (0 when page = 1)
next   = page + 1  (0 when page = pages)
```

Templates hide the pager when `pages IS #1` and each button when its number `IS #0`.

### 1.8 Errors

`error.html` takes [number, MESSAGE]; it prints `E<number> <MESSAGE>.` and
`PLEASE CORRECT SOURCE AND RESUBNIT`. Use exactly these:

| Line | When | `;49221` |
|---|---|---|
| (404) | topic id not found | `#404`, `NO SUCH TOPIC` |
| (404) | post id not found, deleted when the action needs a live post, or quoting a post from another topic | `#404`, `NO SUCH POST` |
| (404) | category not found (GET /c/:id, POST /t/:id/move) | `#404`, `NO SUCH CATEGORY` |
| (404) | user not found | `#404`, `NO SUCH USER` |
| (404) | page number past the last page | `#404`, `NO SUCH PAGE` |
| (404) | catch-all route | `#404`, `NO SUCH PAGE` |
| (403) | guest POSTs anything that needs an account | `#403`, `PLEASE SIGN IN FIRST` |
| (403) | edit/delete a post that is not yours, as a member | `#403`, `THAT POST IS NOT YOURS` |
| (403) | PLEASE your own post | `#403`, `YOU CANNOT PLEASE YOURSELF. THAT WOULD BE OVERLY POLITE` |
| (403) | reply to / quote in a locked topic | `#403`, `THIS TOPIC IS LOCKED` |
| (403) | pin/lock/move as a member | `#403`, `ONLY MODERATORS MAY DO THAT` |
| (403) | role change as non-admin | `#403`, `ONLY THE ADMIN MAY DO THAT` |
| (403) | role change on yourself or on an admin | `#403`, `ADMINS CANNOT BE PROMOTED OR DEMOTED` |
| (400) | role field not 1 or 2 | `#400`, `THAT ROLE DOES NOT EXIST` |

The framework itself answers 403 `YOU CAN'T GET THERE FROM HERE` for cross-origin requests.
Guests asking for a GET page that needs an account (`/new`, `/settings`, quote, edit) are
redirected to `/login?next=<that page>` instead (`to_login.txt`, §1.10).

### 1.9 Validation messages (form re-render, status 200)

Text fields are "blank" when they are empty or contain only spaces, tabs, CR and LF bytes.
Limits are bytes (`.4401`).

| Field | Rule | Message |
|---|---|---|
| category (new topic) | must exist (`HOW MANY` on #2 HAVING #0) | `E422 PLEASE CHOOSE A CATEGORY` |
| title | not blank | `E422 YOUR TOPIC HAS NO TITLE` |
| title | ≤ 120 bytes | `E422 YOUR TITLE IS LONGER THAN 120 BYTES` |
| body | not blank | `E422 YOUR POST IS EMPTY` |
| body | ≤ 20 000 bytes | `E422 YOUR POST IS LONGER THAN 20 000 BYTES` |
| bio | ≤ 2 000 bytes (may be empty) | `E422 YOUR BIO IS LONGER THAN 2 000 BYTES` |
| username | 3–20 bytes, each `A-Z a-z 0-9 _ -` | `E422 USERNAMES ARE 3 TO 20 LETTERS, DIGITS, _ OR -` |
| username | unused (`HOW MANY` on #0 HAVING #1 — case-insensitive by collation) | `E422 THAT USERNAME IS TAKEN` |
| password | ≥ 8 bytes | `E422 PASSWORDS NEED AT LEAST 8 BYTES` |
| confirm | byte-equal to password | `E422 THE PASSWORDS DO NOT MATCH` |
| sign in | name + password verify | `E401 THAT NAME AND PASSWORD DO NOT MINGLE` |

Check in the order listed; report the first failure only. The re-rendered form keeps every
value the user typed except passwords.

### 1.10 Return path and notices

**Return path.** Pages fill `,101` byte by byte (string building, labels (700)–(770)):

| Page | `,101` |
|---|---|
| `/` | `/` |
| `/latest`, `/c/:id`, `/t/:id` | `/latest`, `/c/N`, `/t/N`, plus `?page=N` when not on page 1 |
| `/u/:name` | `/u/<name as requested>` |
| `/search` | `/search` (the query is not carried: templates have no URL encoding) |
| guards on `/new`, `/t/:id/reply`, `/p/:id/edit`, `/settings` | `/new` (`?c=N` when given), `/t/N/reply` (`?quote=N` when given), `/p/N/edit`, `/settings` |
| `/login`, `/register` | the `next` field after (730) |
| error pages | whatever the handler had built, often empty |

Templates link `/login?next={{,101}}` and `/register?next={{,101}}`; the sign-in and register
forms carry it in `<input type="hidden" name="next">`. (730) keeps `next` only when it is 1–1000
bytes, starts with `/`, its second byte is neither `/` nor `\`, and every byte is printable ASCII
33–126 other than `#` (so no CR/LF, spaces, `//host`, `/\host`, `https://…` or fragments).
Anything else becomes empty, and an empty return path means `/`.

**Notices.** After an action the redirect carries `?done=N` (or `&done=N` when the target
already has a query); (900) reads `done`, keeps it only when it is 1–15, and puts it in `.105`;
`_notice.html` (included by `_nav.html`) shows the matching line of a fixed table, never text
from the request. The notice lives only in that one URL.

| N | After | Text after "PLEASE NOTE:" |
|---|---|---|
| 1 | POST /register | you are registered and signed in. Welcome to the Interstate. |
| 2 | POST /login | you are signed in. |
| 3 | POST /logout | you have GIVEN UP. You are signed out. |
| 4 | POST /new | your topic was posted. |
| 5 | POST /t/:id/reply | your reply was posted. |
| 6 | POST /p/:id/edit | your changes were saved. |
| 7 | POST /p/:id/delete | the post was ABSTAINED FROM (deleted). |
| 8 / 9 | POST /t/:id/pin | the topic is pinned to the top of its category. / the topic is no longer pinned. |
| 10 / 11 | POST /t/:id/lock | the topic is locked. No more replies. / the topic is unlocked. Replies are open again. |
| 12 | POST /t/:id/move | the topic was moved. |
| 13 / 14 | POST /u/:name/role | that user is now a moderator. / that user is now a member. |
| 15 | POST /settings | your bio was saved. |

The PLEASE toggle has no notice (the button already shows its state).

---

## 2. Database

Tables and views are numbered by creation order in `schema.sql`; columns by declaration order.
Column #0 of every table is its `INTEGER PRIMARY KEY`; every view also has the row id in #0.
Use the primary-key lookup shape (`'FROM DATABASE #t~#id'` with no clauses) **only on tables**;
on views always write `HAVING #0 x`. To count a whole table or view, give `HOW MANY` a clause
that every row satisfies (`HAVING #0 BEYOND #0 HOW MANY`) so the expression is never read as a
primary-key lookup.

### Tables

| # | Table | Columns |
|---|---|---|
| 0 | users | #0 id, #1 name (`COLLATE NOCASE`, unique), #2 password (`PASSWORD`: hashed on write, `HAVING` verifies), #3 role (1 member, 2 moderator, 3 admin), #4 joined, #5 bio |
| 1 | sections | #0 id, #1 name, #2 position |
| 2 | categories | #0 id, #1 section_id, #2 name, #3 description, #4 color, #5 position |
| 3 | topics | #0 id, #1 category_id, #2 user_id, #3 title, #4 created, #5 bumped (last reply time), #6 pinned 0/1, #7 locked 0/1, #8 views |
| 4 | posts | #0 id, #1 topic_id, #2 user_id, #3 body, #4 created, #5 edited (0 or time), #6 deleted (0 live, 1 by author, 2 by moderator) |
| 5 | pleases | #0 id, #1 post_id, #2 user_id, #3 created; unique (post_id, user_id) |

Inserts (`STASH ;n INTO DATABASE #t`, `;n SUB k` → column k) always fill every column 1..N:

- users: `[name, password, role, now, ""]`
- topics: `[category, user, title, now, now, #0, #0, #0]`
- posts: `[topic, user, body, now, #0, #0]`
- pleases: `[post, user, now]`

### Views (read only)

**#6 board_rows** — one row per category.
#0 id, #1 section_id, #2 section_name, #3 sort_key, #4 first_in_section, #5 name,
#6 description, #7 color, #8 topic_count, #9 post_count, #10 last_post_id, #11 last_topic_id,
#12 last_topic_title, #13 last_poster, #14 last_post_at.
Always order `BY #3 BY #0`; `first_in_section` is 1 on the first category of each section in that
order. `last_*` describe the newest live post (0 / empty when none). Counts include live posts only.

```
'FROM DATABASE #6~#0$#2$#4$#5$#6$#7$#8$#9$#10$#12$#13$#14' BY #3 BY #0 IN BULK
SQL: SELECT … FROM board_rows ORDER BY sort_key, id
1 | The Interstate | 1 | Announcements                | 3 | 9  | 9  | INTERCAL on Interstates 1.1 is out (it's real this time) | butchered_roman
2 | The Interstate | 0 | COME FROM Considered Helpful | 5 | 15 | 25 | gross answers 403 YOU CAN'T GET THERE FROM HERE behind nginx | spark_ears
3 | Rest Stops     | 1 | Show & Tell                  | 3 | 10 | 35 | My error page, in the house style | spark_ears
4 | Rest Stops     | 0 | ABSTAINed (off-topic)        | 3 | 29 | 65 | Post your terminal setup (punched cards welcome) | e774
(columns shown: id, section_name, first_in_section, name, topic_count, post_count, last_post_id, last_topic_title, last_poster)
```

**#7 topic_rows** — one row per topic.
#0 id, #1 category_id, #2 category_name, #3 category_color, #4 title, #5 user_id, #6 author,
#7 created, #8 bumped, #9 pinned, #10 locked, #11 views, #12 reply_count, #13 post_count,
#14 last_post_id, #15 last_poster, #16 recent1, #17 recent2, #18 recent3, #19 recent4,
#20 first_post_id.
post_count counts live posts; reply_count = post_count − 1 (≥ 0); recent1..4 are distinct live
posters other than the author, most recent first (empty when fewer).

```
'FROM DATABASE #7~#0$#4$#1$#2$#3$#6$#16$#17$#18$#19$#12$#11$#8$#9$#10$#14' BY #8 BACKWARDS BY #0 BACKWARDS TAKE #20 IN BULK
SQL: SELECT id, title, category_name, author, recent1, recent2, reply_count, views, pinned, locked, last_post_id
     FROM topic_rows ORDER BY bumped DESC, id DESC LIMIT 4
14 | Post your terminal setup (punched cards welcome) | ABSTAINed (off-topic) | onespot | e774 | tailarray | 2 | 41 | 0 | 0 | 65
8  | gross answers 403 YOU CAN'T GET THERE FROM HERE behind nginx | COME FROM Considered Helpful | newbie | spark_ears | | 1 | 19 | 0 | 0 | 25
7  | YOU CAN ONLY COME FROM A PAGE. What does it mean? | COME FROM Considered Helpful | politeness | spark_ears | | 2 | 87 | 0 | 0 | 23
13 | Is it rude to end every message with PLEASE GIVE UP? | ABSTAINed (off-topic) | politeness | newbie | butchered_roman | 2 | 133 | 0 | 0 | 62
```

**#8 post_rows** — one row per post, deleted ones included with an empty body.
#0 id, #1 topic_id, #2 user_id, #3 author, #4 author_role, #5 role_label, #6 author_joined,
#7 author_posts, #8 body, #9 created, #10 edited, #11 deleted, #12 please_count, #13 number,
#14 topic_title, #15 topic_locked, #16 category_id.
`number` is the 1-based position in the topic counting every post (so page = (number + 19) / 20).

```
'FROM DATABASE #8~#0$#2$#3$#4$#5$#6$#7$#8$#9$#10$#11$#12$#13$#1$#15' HAVING #1 #6 BY #0 SKIP #0 TAKE #20 IN BULK
SQL: SELECT id, author, role_label, author_posts, substr(body,1,30), edited>0, deleted, please_count, number
     FROM post_rows WHERE topic_id = 6 ORDER BY id
17 | come_from  | member    | 7 | `'FROM DATABASE #3~#3' HAVING  | 0 | 0 | 2 | 1
18 | spark_ears | moderator | 8 | Mingle the column numbers: …   | 1 | 0 | 3 | 2
19 | come_from  | member    | 7 | So the `$` in the selector doe | 0 | 0 | 0 | 3
20 | e774       | member    | 6 |                                | 0 | 1 | 0 | 4
```

**#9 user_rows** — profile header.
#0 id, #1 name, #2 role, #3 role_label, #4 joined, #5 bio, #6 post_count, #7 topic_count,
#8 pleases_received, #9 pleases_given, #10 last_post_at (0 = never).
`HAVING #1 name` is case-insensitive (the column keeps `users.name`'s NOCASE collation).

```
'FROM DATABASE #9~#0$#1$#2$#3$#4$#5$#6$#7$#8$#9$#10' HAVING #1 ,1
SQL: SELECT id, name, role_label, post_count, topic_count, pleases_received, pleases_given
     FROM user_rows WHERE name = 'SPARK_EARS'
2 | spark_ears | moderator | 8 | 0 | 12 | 8
```

**#10 post_index** — search results and profile "recent posts".
#0 id, #1 topic_id, #2 title, #3 user_id, #4 author, #5 created, #6 excerpt, #7 deleted,
#8 category_id, #9 category_name, #10 category_color, #11 number, #12 is_first, #13 haystack.
`haystack` = title + body for a topic's opening post, body for replies, so one
`HAVING #13 SOMEWHERE q` finds topics by title and posts by body. Always add `HAVING #7 #0`.
`excerpt` is the first 280 bytes of the markdown on one line.

```
'FROM DATABASE #10~#0$#1$#2$#4$#5$#6$#9$#10$#12$#8' HAVING #13 SOMEWHERE ,1 HAVING #7 #0 BY #5 BACKWARDS BY #0 BACKWARDS SKIP #0 TAKE #20 IN BULK
SQL: SELECT id, topic_id, title, author, is_first, substr(excerpt,1,40)
     FROM post_index WHERE haystack LIKE '%lagoon%' AND deleted = 0 ORDER BY created DESC, id DESC
39 | 12 | What's your favourite INTERCAL error message? | tailarray | 0 | ICL123I PROGRAM HAS DISAPPEARED INTO THE
```

**#11 forum_stats** — exactly one row, id 1.
#0 id, #1 users, #2 topics, #3 posts (live), #4 pleases, #5 newest_member.

```
'FROM DATABASE #11~#1$#2$#3$#4$#5' HAVING #0 #1
SQL: SELECT * FROM forum_stats
1 | 10 | 14 | 63 | 91 | newbie
```

---

## 3. Routes

Declare the COME FROMs in this order (first match wins; the catch-all must be last).

| # | Method + pattern | Origin | Renders / answers |
|---|---|---|---|
| 1 | `GET /` | 16000 | index.html at (100) |
| 2 | `GET /latest` | 16000 | latest.html at (110) |
| 3 | `GET /c/:id` | 16000 | category.html at (120) |
| 4 | `GET /t/:id` | 16000 | topic.html at (130) |
| 5 | `GET /p/:id` | 16000 | 303 → `/t/:topic?page=N#p:id` |
| 6 | `GET /t/:id/reply` | (130) | compose.html at (140), mode 2 |
| 7 | `POST /t/:id/reply` | 16000 | 303 → `/p/:new` or compose.html (140) |
| 8 | `GET /new` | 16000 | compose.html at (140), mode 1 |
| 9 | `POST /new` | (140) | 303 → `/t/:new` or compose.html (140) |
| 10 | `GET /p/:id/edit` | (130) | compose.html at (140), mode 3 |
| 11 | `POST /p/:id/edit` | (140) | 303 → `/p/:id` or compose.html (140) |
| 12 | `POST /p/:id/delete` | (130) | 303 → `/p/:id` |
| 13 | `POST /p/:id/please` | (130) | 303 → `/p/:id` |
| 14 | `POST /t/:id/pin` | (130) | 303 → `/t/:id` |
| 15 | `POST /t/:id/lock` | (130) | 303 → `/t/:id` |
| 16 | `POST /t/:id/move` | (130) | 303 → `/t/:id` |
| 17 | `GET /u/:name` | 16000 | profile.html at (170) |
| 18 | `POST /u/:name/role` | (170) | 303 → `/u/:name` |
| 19 | `GET /settings` | 16000 | settings.html at (180) |
| 20 | `POST /settings` | (180) | 303 → `/u/:me` or settings.html (180) |
| 21 | `GET /search` | 16000 | search.html at (190) |
| 22 | `GET /login` | 16000 | login.html at (150) |
| 23 | `POST /login` | (150) | 303 → `/` or login.html (150) |
| 24 | `GET /register` | 16000 | register.html at (160) |
| 25 | `POST /register` | (160) | 303 → `/` or register.html (160) |
| 26 | `POST /logout` | 16000 | 303 → `/` |
| 27 | `*` | 16000 | error.html at (404) |

Non-16000 origins and every POST need a same-host `Origin` or `Referer` header (the
framework checks; curl tests must send `-H "Origin: http://127.0.0.1:PORT"` or `-e`). The quote
and edit pages are GETs with origin (130): they are only reachable from a topic page.

Below, `now` is `WRITE IN :9 WITH TIME`, `me` is `:100`, `[a, b, …]` is the `;49221` content in
order, and "the categories LIST" is `'FROM DATABASE #6~#0$#5' BY #3 BY #0 IN BULK`.

### 3.1 `GET /` — board index → (100) index.html

1. `.104 <- #1`.
2. `[ rows, stats ]`:
   - rows `'FROM DATABASE #6~#0$#2$#4$#5$#6$#7$#8$#9$#10$#12$#13$#14' BY #3 BY #0 IN BULK`
   - stats `'FROM DATABASE #11~#1$#2$#3$#4$#5' HAVING #0 #1`

Template: `;144` rows — loop vars `,144` id, `,143` section name, `,142` first_in_section,
`,141` name, `,140` description, `,139` color, `,138` topics, `,137` posts, `,136` last post id,
`,135` last topic title, `,134` last poster, `,133` last post time. `;143 SUB #1..#5` stats.

### 3.2 `GET /latest` — latest topics → (110) latest.html

1. `.104 <- #2`.
2. n = `'FROM DATABASE #7~#0' HAVING #0 BEYOND #0 HOW MANY`; paginate (§1.7).
3. `[ rows, page, pages, prev, next ]`, rows =
   `'FROM DATABASE #7~#0$#4$#1$#2$#3$#6$#16$#17$#18$#19$#12$#11$#8$#9$#10$#14' BY #8 BACKWARDS BY #0 BACKWARDS SKIP skip TAKE #20 IN BULK`

This 16-column selector is the **topic row** shape read by `_topic_row.html`: `,144` id,
`,143` title, `,142` category id, `,141` category name, `,140` color, `,139` author,
`,138`–`,135` recent posters, `,134` replies, `,133` views, `,132` bumped, `,131` pinned,
`,130` locked, `,129` last post id.

### 3.3 `GET /c/:id` — category → (120) category.html

1. id = field "id". `'FROM DATABASE #2~#0' HAVING #0 id HOW MANY` = 0 → 404 NO SUCH CATEGORY.
2. n = `'FROM DATABASE #7~#0' HAVING #1 id HOW MANY`; paginate.
3. `;49221 <- 'FROM DATABASE #6~#0$#5$#6$#7$#2$#8' HAVING #0 id` (k1–k6: id, name,
   description, color, section name, topic count), then
   k7 rows = topic row shape `HAVING #1 id BY #9 BACKWARDS BY #8 BACKWARDS BY #0 BACKWARDS SKIP skip TAKE #20 IN BULK`
   (pinned first, then last activity), k8 page, k9 pages, k10 prev, k11 next.

"New topic" links to `/new?c=:id` for signed-in users, "Sign in to post" otherwise.

### 3.4 `GET /t/:id` — topic → (130) topic.html

1. id = field "id". `'FROM DATABASE #3~#0' HAVING #0 id HOW MANY` = 0 → 404 NO SUCH TOPIC.
2. Count the view first: `:2 <- 'FROM DATABASE #3~#8' HAVING #0 id`, add 1 with (1500),
   `'FROM DATABASE #3~#8' HAVING #0 id <- :3`.
3. n = `'FROM DATABASE #4~#0' HAVING #1 id HOW MANY` (all posts, deleted included); paginate.
4. `;49221 <- 'FROM DATABASE #7~#0$#4$#1$#2$#3$#9$#10$#11$#13$#20' HAVING #0 id`
   (k1–k10: id, title, category id, category name, color, pinned, locked, views, live post
   count, first post id), then
   - k11 posts `'FROM DATABASE #8~#0$#2$#3$#4$#5$#6$#7$#8$#9$#10$#11$#12$#13$#1$#15' HAVING #1 id BY #0 SKIP skip TAKE #20 IN BULK`
   - k12 page, k13 pages, k14 prev, k15 next
   - k16 the categories LIST (move form; moderators only see it, always pass it)
5. `;100`: dimension to 20, zero it; when signed in, for each row k of the posts LIST,
   `;100 SUB k <- 'FROM DATABASE #5~#0' HAVING #1 <post id of row k> HAVING #2 :100 HOW MANY`.

Post loop vars: `,144` post id, `,143` author id, `,142` author, `,141` role, `,140` role
label, `,139` joined, `,138` author post count, `,137` body, `,136` created, `,135` edited,
`,134` deleted, `,133` PLEASE count, `,132` number, `,131` topic id, `,130` topic locked.

The template decides per post, from `:100`, `.102` and the row:
- PLEASE button: signed in and not the author. Count only (no button) for guests and own posts.
- Quote: signed in and topic not locked.
- Edit and Delete: moderator, or the author.
- Deleted posts show a tombstone ("ABSTAINED FROM by its author / by a moderator"), no actions.
- Reply form: signed in and not locked. Locked notice when locked; sign-in prompt for guests.
- Moderator tools (pin/unpin, lock/unlock, move): `.102`.

### 3.5 `GET /p/:id` — permalink

1. `'FROM DATABASE #8~#1$#13' HAVING #0 id` → [topic id, number]; empty → 404 NO SUCH POST.
2. page = (number + 19) / 20. Redirect `to_topic_page.txt` with [topic id, page, id, `done`
   field] so a notice survives the permalink hop.

Deleted posts keep working permalinks (they land on their tombstone).

### 3.6 `GET /t/:id/reply` (origin 130) — reply editor, optionally quoting → (140) mode 2

1. Guest → redirect `to_login.txt` with `,101` = `/t/N/reply` (`?quote=Q` when quoting).
2. Topic must exist (404 NO SUCH TOPIC) and not be locked (403 THIS TOPIC IS LOCKED):
   `'FROM DATABASE #7~#4$#1$#10' HAVING #0 id` → [title, category id, locked].
3. quote = `WRITE IN :2 WITH FIELD "quote"` (0 when absent). When non-zero:
   `'FROM DATABASE #8~#3$#8$#1$#11' HAVING #0 :2` → [author, body, topic id, deleted]; missing,
   another topic, or deleted → 404 NO SUCH POST.
4. compose params (§4): `[#2, id, category id, title, "", "", #0, author or "", body or "", #0, categories, id]`.

### 3.7 `POST /t/:id/reply` — post a reply

Fields: `body`, `preview`.
1. Guest → 403 PLEASE SIGN IN FIRST. Look the topic up as in 3.6 (`#7~#4$#1$#10`): missing →
   404 NO SUCH TOPIC; locked → 403 THIS TOPIC IS LOCKED.
2. Validate body (§1.9). On failure, or when `preview` is set: render compose
   `[#2, id, category id, title, body, message or "", #1 if preview else #0, "", "", #0, categories, id]`.
3. Insert post `[id, me, body, now, #0, #0]` into #4; `'FROM DATABASE #3~#5' HAVING #0 id <- now`
   (bump). Redirect `to_post.txt` with [`:4400`, `#5`].

### 3.8 `GET /new` — new topic editor → (140) mode 1

Guest → redirect `to_login.txt` (`,101` = `/new`, `?c=N` when given). c = `WRITE IN .2 WITH FIELD "c"` (0 when absent; preselects).
compose params: `[#1, #0, c, "", "", "", #0, "", "", #1, categories, #0]`.

### 3.9 `POST /new` (origin 140) — create topic

Fields: `category` (number), `title`, `body`, `preview`.
1. Guest → 403 PLEASE SIGN IN FIRST.
2. Validate category, title, body (§1.9). On failure or preview: render compose
   `[#1, #0, category, title, body, message or "", preview flag, "", "", #1, categories, #0]`.
3. Insert topic `[category, me, title, now, now, #0, #0, #0]` into #3 → topic id = `:4400`;
   insert post `[topic id, me, body, now, #0, #0]` into #4. Redirect `to_topic.txt` [topic id, `#4`].

### 3.10 `GET /p/:id/edit` (origin 130) — edit editor → (140) mode 3

1. Guest → redirect `to_login.txt` (`,101` = `/p/N/edit`).
2. `'FROM DATABASE #8~#1$#2$#8$#13$#14$#16$#11' HAVING #0 id` → [topic id, author id, body,
   number, topic title, category id, deleted]; missing or deleted → 404 NO SUCH POST.
3. Not author and not `.102` → 403 THAT POST IS NOT YOURS.
4. compose params: `[#3, id, category id, topic title, body, "", #0, "", "", #1 if number = 1 else #0, categories, topic id]`.

### 3.11 `POST /p/:id/edit` (origin 140) — save edit

Fields: `title` (read only when editing the opening post), `body`, `preview`.
1. Guest → 403. Same lookups and checks as 3.10.
2. Validate (title only when number = 1). On failure or preview: render compose
   `[#3, id, category id, title (typed, or the topic title), body, message or "", preview flag, "", "", title editable flag, categories, topic id]`.
3. `'FROM DATABASE #4~#3$#5' HAVING #0 id <- [body, now]` (body + edited time); when number = 1
   also `'FROM DATABASE #3~#3' HAVING #0 topic id <- title`. Redirect `to_post.txt` [id, `#6`].

Editing stays allowed in locked topics.

### 3.12 `POST /p/:id/delete` (origin 130) — soft delete

1. Guest → 403. Post missing or already deleted → 404 NO SUCH POST.
2. Author → deleted = 1; else moderator → deleted = 2; else 403 THAT POST IS NOT YOURS.
   `'FROM DATABASE #4~#6' HAVING #0 id <- flag`.
3. `FORGET 'FROM DATABASE #5' HAVING #1 id` (its PLEASEs go with it).
4. Redirect `to_post.txt` [id, `#7`].

### 3.13 `POST /p/:id/please` (origin 130) — PLEASE toggle

1. Guest → 403. `'FROM DATABASE #4~#2$#6' HAVING #0 id` → [author, deleted]; missing or deleted
   → 404 NO SUCH POST. Author = me → 403 YOU CANNOT PLEASE YOURSELF….
2. `'FROM DATABASE #5~#0' HAVING #1 id HAVING #2 me HOW MANY`: 1 → `FORGET 'FROM DATABASE #5' HAVING #1 id HAVING #2 me`;
   0 → insert `[id, me, now]` into #5.
3. Redirect `to_post.txt` [id, `#0`] (no notice).

### 3.14–3.16 Moderation (origin 130): pin, lock, move

All: guest → 403 PLEASE SIGN IN FIRST; not `.102` → 403 ONLY MODERATORS MAY DO THAT; topic
missing → 404 NO SUCH TOPIC; redirect `to_topic.txt` [id, notice]: pin `#8`/unpin `#9`,
lock `#10`/unlock `#11`, move `#12`.
- `POST /t/:id/pin`, field `pinned`: `#1` if it is 1, else `#0` → `'FROM DATABASE #3~#6' HAVING #0 id <- v`.
- `POST /t/:id/lock`, field `locked`: same → column #7.
- `POST /t/:id/move`, field `category`: must exist (404 NO SUCH CATEGORY) → column #1.

### 3.17 `GET /u/:name` — profile → (170) profile.html

1. name = field "name". `'FROM DATABASE #9~#0' HAVING #1 name HOW MANY` = 0 → 404 NO SUCH USER.
2. `;49221 <- 'FROM DATABASE #9~#0$#1$#2$#3$#4$#5$#6$#7$#8$#9$#10' HAVING #1 name` (k1–k11), then
   k12 recent `'FROM DATABASE #10~#0$#2$#5$#6$#9$#10$#12$#8' HAVING #3 <k1 user id> HAVING #7 #0 BY #5 BACKWARDS BY #0 BACKWARDS TAKE #10 IN BULK`.

Recent loop vars: `,144` post id, `,143` topic title, `,142` created, `,141` excerpt,
`,140` category name, `,139` color, `,138` is first post, `,137` category id.
The admin box (promote / demote) shows when `.103`, the profile is not your own, and the target
is not an admin. "Edit your bio" shows on your own profile.

### 3.18 `POST /u/:name/role` (origin 170) — promote / demote

Field `role`. Guest → 403 PLEASE SIGN IN FIRST. Not `.103` → 403 ONLY THE ADMIN MAY DO THAT.
`'FROM DATABASE #0~#0$#3$#1' HAVING #1 name` → [id, role, canonical name]; missing → 404 NO SUCH
USER. id = me or role = 3 → 403 ADMINS CANNOT BE PROMOTED OR DEMOTED. role field not 1 or 2 →
400 THAT ROLE DOES NOT EXIST. `'FROM DATABASE #0~#3' HAVING #0 id <- role`. Redirect `to_user.txt`
[canonical name, `#13` promoted / `#14` demoted].

### 3.19 `GET /settings` → (180) settings.html

Guest → redirect `to_login.txt` (`,101` = `/settings`). `[ 'FROM DATABASE #0~#5' HAVING #0 me, "", #0 ]` (bio, error, preview).

### 3.20 `POST /settings` (origin 180)

Fields `bio`, `preview`. Guest → 403. Too long, or preview → render `[bio, message or "", preview flag]`.
Else `'FROM DATABASE #0~#5' HAVING #0 me <- bio`; redirect `to_user.txt` [`,100`, `#15`].

### 3.21 `GET /search` → (190) search.html

1. `.104 <- #3`. q = `WRITE IN ,1 WITH FIELD "q"`; length `.4401`.
2. Empty q → `[ "", <empty LIST>, #0, #1, #1, #0, #0 ]`.
3. Else n = `'FROM DATABASE #10~#0' HAVING #13 SOMEWHERE ,1 HAVING #7 #0 HOW MANY`; paginate;
   `[ q, rows, n, page, pages, prev, next ]`, rows =
   `'FROM DATABASE #10~#0$#1$#2$#4$#5$#6$#9$#10$#12$#8' HAVING #13 SOMEWHERE ,1 HAVING #7 #0 BY #5 BACKWARDS BY #0 BACKWARDS SKIP skip TAKE #20 IN BULK`.

Result loop vars: `,144` post id, `,143` topic id, `,142` topic title, `,141` author,
`,140` created, `,139` excerpt, `,138` category name, `,137` color, `,136` is first post,
`,135` category id. Each result links to `/p/:post` (the matching post, on the right page).
Pagination buttons are GET forms with `q` and `page`, so the browser encodes the query.

### 3.22 `GET /login` → (150) login.html

`next` → `,101` via (730). Signed in → redirect to the return path (`to_next.txt`, `/` when
empty, no notice). Else `[ "", "" ]` (error, name); the form carries `,101`.

### 3.23 `POST /login` (origin 150)

Fields `name`, `password`, `next` (via (730)). `'FROM DATABASE #0~#0' HAVING #1 ,name HAVING #2 ,password HOW MANY`
(PASSWORD column: HAVING verifies the hash) = 0 → render `[ "E401 THAT NAME AND PASSWORD DO NOT MINGLE", name ]`.
Else id = `'FROM DATABASE #0~#0' HAVING #1 ,name`; `READ OUT id WITH SESSION`; redirect `to_next.txt`
to the return path (or `/`) with notice 2.

### 3.24 `GET /register` → (160) register.html; as GET /login: `next` via (730), signed in → redirect to the return path. `[ "", "" ]`.

### 3.25 `POST /register` (origin 160)

Fields `name`, `password`, `confirm`, `next` (via (730)). Validate (§1.9) → on failure render `[ message, name ]`.
role = `#3` if `'FROM DATABASE #0~#0' HAVING #0 BEYOND #0 HOW MANY` = 0 (first account is admin), else `#1`.
Insert `[name, password, role, now, ""]` into #0; `READ OUT :4400 WITH SESSION`; redirect `to_next.txt`
to the return path (or `/`) with notice 1.

### 3.26 `POST /logout`

`READ OUT #0 WITH SESSION`; redirect `to_next.txt` to `/?done=3`. ("GIVE UP" in the header.)

### 3.27 `*` — catch-all

Error (404) `[ #404, "NO SUCH PAGE" ]`.

---

## 4. Template parameter reference

`k` is the position in `;49221`; the variable is `,145−k` (or `;145−k` for LISTs).

| Template | k → variable |
|---|---|
| index.html | 1 `;144` board rows · 2 `;143` stats |
| latest.html | 1 `;144` topic rows · 2 `,143` page · 3 `,142` pages · 4 `,141` prev · 5 `,140` next |
| category.html | 1 `,144` id · 2 `,143` name · 3 `,142` description · 4 `,141` color · 5 `,140` section · 6 `,139` topic count · 7 `;138` topic rows · 8 `,137` page · 9 `,136` pages · 10 `,135` prev · 11 `,134` next |
| topic.html | 1 `,144` id · 2 `,143` title · 3 `,142` category id · 4 `,141` category name · 5 `,140` color · 6 `,139` pinned · 7 `,138` locked · 8 `,137` views · 9 `,136` live posts · 10 `,135` first post id · 11 `;134` post rows · 12 `,133` page · 13 `,132` pages · 14 `,131` prev · 15 `,130` next · 16 `;129` categories |
| compose.html | 1 `,144` mode (1 new, 2 reply, 3 edit) · 2 `,143` target (topic id in mode 2, post id in mode 3, 0) · 3 `,142` category id · 4 `,141` title · 5 `,140` body · 6 `,139` error · 7 `,138` preview flag · 8 `,137` quoted author · 9 `,136` quoted body · 10 `,135` title editable · 11 `;134` categories · 12 `,133` topic id for Cancel (0 in mode 1) |
| login.html | 1 `,144` error · 2 `,143` name |
| register.html | 1 `,144` error · 2 `,143` name |
| profile.html | 1 `,144` id · 2 `,143` name · 3 `,142` role · 4 `,141` role label · 5 `,140` joined · 6 `,139` bio · 7 `,138` posts · 8 `,137` topics · 9 `,136` PLEASEs received · 10 `,135` PLEASEs given · 11 `,134` last post time · 12 `;133` recent posts |
| settings.html | 1 `,144` bio · 2 `,143` error · 3 `,142` preview flag |
| search.html | 1 `,144` q · 2 `;143` results · 3 `,142` total · 4 `,141` page · 5 `,140` pages · 6 `,139` prev · 7 `,138` next |
| error.html | 1 `,144` number · 2 `,143` message |

Partials: `_head.html` (inside `<head>` after `<title>`), `_nav.html` (reads `:100`, `,100`,
`.104`, `,101`; includes `_notice.html`, which reads `.105`), `_foot.html`, `_topic_row.html`
(topic row shape, inside a loop), `_post_owner.html` (Edit/Delete, topic.html only).

### Static link check summary

| Template (line) | Links and forms | Matching route origins |
|---|---|---|
| every page via `_nav`/`_foot` | `/`, `/latest`, GET `/search`, `/new`, `/u/:name`, `/login`, `/register`, POST `/logout` | 16000 |
| index (100) | `/c/:id`, `/u/:name`, `/p/:id` | 16000 |
| latest (110) | `/t/:id`, `/c/:id`, `/u/:name`, `/p/:id`, `/latest?page`, `/new` | 16000 |
| category (120) | the same, `/c/:id?page`, `/new?c` | 16000 |
| topic (130) | `/c/:id`, `/u/:name`, `/p/:id`, `/t/:id?page`, POST `/t/:id/reply` | 16000 |
| | GET `/t/:id/reply?quote`, GET `/p/:id/edit`, POST `/p/:id/please`, `/p/:id/delete`, `/t/:id/pin`, `/t/:id/lock`, `/t/:id/move` | (130) |
| compose (140) | `/t/:id`, `/c/:id`, `/`, POST `/t/:id/reply` | 16000 |
| | POST `/new`, POST `/p/:id/edit` | (140) |
| login (150) | `/register` · POST `/login` | 16000 · (150) |
| register (160) | `/login` · POST `/register` | 16000 · (160) |
| profile (170) | `/settings`, `/p/:id`, `/c/:id` · POST `/u/:name/role` | 16000 · (170) |
| settings (180) | `/u/:name` · POST `/settings` | 16000 · (180) |
| search (190) | GET `/search`, `/p/:id`, `/c/:id`, `/u/:name` | 16000 |
| error (400/403/404) | `/`, `/latest`, `/search` | 16000 |

---

## 5. Flows

- **Register → first admin.** POST /register on an empty users table gives role 3. With the
  seed loaded, `admin` (id 1) already is the admin and new accounts are members.
- **Sign in / out.** POST /login sets the session to the user id and returns to the page the
  Sign in link came from (`next`, §1.10) with "PLEASE NOTE: you are signed in."; "GIVE UP"
  posts /logout, which sets it to 0. A session naming a vanished user is cleared by (900).
- **Compose, preview, validate.** Every editor POST re-renders compose.html at (140) with the
  user's text when `preview` is set or validation fails (message in `,139`, preview flag in
  `,138`); otherwise it writes and redirects to the new or edited post.
- **Quote.** Quote on a post is `GET /t/:tid/reply?quote=:pid` (origin 130); the template opens the
  textarea with `[quote="author"]\nbody\n[/quote]\n\n`.
- **PLEASE.** POST /p/:id/please toggles and redirects to the permalink; the topic page shows
  the pressed state from `;100` (button label becomes PLEASED via CSS, `aria-pressed="true"`).
- **Edit / delete.** Owners and moderators; delete is soft and shows a tombstone; edit sets
  `edited`, shown as "edited" next to the time. Editing the opening post may change the title.
- **Pin / lock / move.** Moderator tools at the bottom of the topic page. Locked topics hide
  the reply form and Quote, and POST /t/:id/reply answers 403.
- **Promote / demote.** Admin-only box on a member's or moderator's profile.
- **Search.** Header box and /search page; results newest first, 20 per page, each linking to
  the matching post via its permalink.
- **Pagination.** `?page=N` on /latest, /c/:id, /t/:id and /search; out-of-range → 404.
