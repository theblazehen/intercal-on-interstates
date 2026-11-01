# PLEASE DISCUSS — the INTERCAL on Interstates community forum

A phpBB-structured, Discourse-readable forum written in INTERCAL on Interstates. `forum.i`
is the whole program: routing, sessions, permissions, validation, pagination arithmetic and
every database read and write. It is written against [ROUTES.md](ROUTES.md); the product is
described in [PRD.md](PRD.md).

| Path | What |
|---|---|
| `forum.i` | the program (~1400 lines of INTERCAL, about 22 % PLEASE) |
| `schema.sql` | 6 tables + 6 read-only views; numbering is the ActiveCassette contract |
| `seed.sql` | 10 accounts, 2 sections, 4 categories, 14 topics, 65 posts, 91 PLEASEs |
| `templates/` | 11 page templates, 6 partials (`_*.html`), 6 redirect templates (`to_*.txt`) |
| `public/forum.css` | all styling, hand-written, light + dark |
| `public/forum.js` | optional: delete confirmation, Ctrl/Cmd+Enter to post, caret after a quote |
| `public/favicon.svg` | the Interstate-72 shield |
| `ROUTES.md` | every route, origin label, query, template parameter, message and redirect |
| `tests/run.sh` | curl end-to-end test (also run by `make check` via `tests/forum/run.sh`) |
| `preview/*.html` | static pages rendered from the real templates and seed data |
| `preview/shots/` | screenshots of the previews: desktop 1280 px and mobile 390 px, light and dark |
| `preview/shots-live/` | screenshots of the running forum in Chromium (same naming), from the browser QA pass; `notice-*` show a PLEASE NOTE banner after sign-in |

## Running

```
mise run forum            # builds ick/gross, compiles forum.i, serves http://localhost:10585
```

By hand, from `forum/`: `../build/ick -s -b forum.i && ../build/gross ./forum`. The database
is `forum/cassette.db` (override with `IOI_DATABASE`); the first request on a missing or empty
database runs `schema.sql` and `seed.sql`. Delete `cassette.db*` for a fresh forum. Run from a
shell (`./forum`), the program prints the board index, which also creates the database.

To start with no accounts at all (so the first registration becomes the admin), serve with
`IOI_APP_DIR` pointing at a directory that holds only `schema.sql`; templates are compiled into
the binary, so the app directory only supplies the SQL.

## Tests

`sh forum/tests/run.sh` (from anywhere; `KEEP=1` keeps the temp dir) compiles `forum.i` into a
temp dir and serves it twice on random ports:

1. **Empty forum** (schema only): the first registered account is the admin, the second a
   member, the admin sees the promote box.
2. **Seeded forum** (temp copy of the database): every page with real data, pagination
   boundaries (page 0, non-numeric, past the end), search, CSRF (no or foreign `Origin`,
   origin-130 GET without `Referer`), every registration and compose validation message, sign
   in/out, topic + reply + preview + quote, edit (own, someone else's → 403, moderator), soft
   delete by author and moderator, PLEASE/un-PLEASE and PLEASE-yourself → 403, pin/lock/move
   (locked topics refuse replies and quotes, even from moderators), promote/demote and its
   403/400/404s, bio settings, XSS payloads in title, body, bio and username rendered inert, and
   the topic page under 100 ms.
   The seeded run also covers the **return path and notices**: Sign in / Register links carry `next` from every page; guards on
   `/new`, reply, edit and settings bounce to `/login?next=…`; sign-in and registration go back
   there; `//evil.example`, `https://evil.example`, `/\evil.example`, CR/LF, spaces, fragments and
   relative paths fall back to `/`; every action's redirect shows its PLEASE NOTE banner, and
   unknown or non-numeric `done` codes show nothing.

It prints `forum: N passed, M failed` and exits non-zero on any failure.

## forum.i — how the program is laid out

The header of `forum.i` repeats this map. Route *n* (ROUTES.md §3 order, which is also the
COME FROM order) lives at labels `2000 + 100n`:

| Labels | What |
|---|---|
| (100)–(190) | the one `READ OUT … WITH TEMPLATE` line of each page (ROUTES.md §1.1). Single-route pages render inline at the end of their handler; compose (140), login (150), register (160) and settings (180) sit in a shared block that their GET and POST handlers `NEXT` to |
| (400) (403) (404) | `error.html` with status 400/403/404 |
| (600)–(690) | form validators: category, title, body, bio, username, password, confirmation; (690) decides save-or-show from the validity flag `.40` and the Preview button |
| (700)–(771) | string building: append bytes (700), a decimal number (710), `?done=N` / `&done=N` (720), validate `next` (730), `?page=N` (750), redirect to the return path (760) |
| (800)–(894) | error raisers, one per ROUTES.md §1.8 message; they never return |
| (900) | the session subroutine every handler calls first (`:100`, `,100`, `.101`–`.104`, and the notice `.105`) |
| (905) (907) (915) (917) | guards: POST needs an account (403), GET needs an account (→ `/login`), moderator only, topic must exist |
| (910) | pagination: rows and requested page in, page/pages/prev/next/skip out, 404 past the end |
| (920)–(928) | byte scanner over a 256-entry class table (blank bytes, username bytes) |
| (930) (950) | "topic open for replies" and "post you may edit or delete" loaders |
| (960) (970) | 32-bit differ and less-than, via the system library's subtraction (1510) |
| (980) | the branch helper: `DO (L) NEXT` / `(L) DO (980) NEXT` runs the code after `(L)` when `.5` is zero |
| (985) | redirect back to the topic after a moderator action |
| 2100–4799 | the 27 route handlers |

INTERCAL has no `IF`. Every decision is a 0/1 flag in `.5` (`'.x~.x'~#1` is "not zero",
`'?.a$.b'~'#0$#65535'` is XOR, `'V.a$.b'~'#0$#65535'` is OR, `.a~.b` is AND of two flags)
followed by the branch idiom above; the zero branch either `RESUME #1`s back to the line after
`DO (L) NEXT` (an IF-THEN), `RESUME #2`s out of the enclosing subroutine, or raises an error and
never comes back. Loops are `COME FROM` the last statement of their body. Arithmetic is the
system library: (1020) for counters, (1500)/(1510)/(1540)/(1550) for pages and views,
(1030) for the topic page's row count. The register handler computes the new account's role
as `z$#1` (mingle), which is 3 for the first account and 1 afterwards.

### Return path and notices (ROUTES.md §1.10)

INTERCAL has no string concatenation, so each page spells its own address into `,101` byte by
byte: `"/t/"` is copied in with (700), the topic id is turned into decimal digits by dividing by
ten with (1550) and stacking the remainders from the right (710), and `?page=N` is added when
the reader is not on page 1. Every Sign in and Register link is `/login?next={{,101}}`. When the
form comes back, (730) checks `next` with the byte scanner: it must start with `/`, its second
byte must be neither `/` nor `\`, and every byte must be printable ASCII other than `#` (so no
CR/LF). Otherwise the return path is `/`.

Notices are a number, not text. Every action's redirect carries `done=N`. (900) keeps N only
when its high half is empty and it is below 16, and `_notice.html` picks the sentence from a
fixed table, so a request can never put its own words into the banner. Whether `?` or `&` comes
before `done` is decided by scanning the path for `?` (720).

### Deviations from ROUTES.md

None in routes, origins, queries, parameters or messages. The return path and notices
(ROUTES.md §1.10) extend the redirects, and `to_root.txt`/`to_category.txt` were removed
(`to_next.txt` replaces the first; the second was never used). Implementation notes:

- The quote check in `GET /t/:id/reply` is a single `HOW MANY` on posts with `HAVING #0 quote
  HAVING #1 topic HAVING #6 #0` instead of fetching the row and comparing three columns.
- `.4401` saturates at 65 535, so the blank-text scan looks at the first 65 535 bytes of a
  field. Longer bodies fail the 20 000-byte limit anyway; a body that is blank for its first
  64 KiB is reported as empty rather than too long.
- Page numbers are read into a 32-bit variable; non-numeric values, values above 4 294 967 295
  and 0 all mean page 1.
- Search pages return to `/search` without the query: templates have no URL-encoding filter
  and the return path is only built from bytes that need none.

## Seed accounts

Passwords are stored in plain text in `seed.sql`; the runtime hashes `PASSWORD` columns on
first open.

| Name | Password | Role |
|---|---|---|
| `admin` | `please-give-up` | admin (user 1, the "first registered account") |
| `spark_ears` | `rabbit-ears` | moderator |
| `onespot` | `mingle-select` | member |
| `tailarray` | `interleave!` | member |
| `come_from` | `computed-come-from` | member |
| `politeness` | `please-please` | member |
| `butchered_roman` | `xlii-xlii` | member |
| `e774` | `random-compiler-bug` | member |
| `newbie` | `hello-world` | member |
| `cheap_watches` | `not-a-spammer` | member (its one post was deleted by a moderator) |

Seed times are relative to the moment the database is created
(`strftime('%s','now') - N`), so a fresh forum always looks recently active. Topic 12 has 23
posts (two pages); topic 2 is pinned and locked; topic 6 has an edited post and an
author-deleted post; topic 13 has a moderator-deleted spam post; topic 8 is a recent question.

## Design decisions

- **Views do the joins, INTERCAL does the logic.** ActiveCassette filters one table or view at
  a time, so every page reads one view per list: `board_rows` (#6), `topic_rows` (#7),
  `post_rows` (#8), `user_rows` (#9), `post_index` (#10), `forum_stats` (#11). Counts and
  "last post" are computed by the views. Permissions, validation, pagination, toggles and
  every write live in forum.i. The only denormalised column is `topics.bumped` (last reply
  time), which forum.i sets when it creates a topic or a reply, so topic lists sort on an
  indexed column.
- **One render line per page template.** Static link checking lets a template link only to
  routes whose origin is 16000 or the line that renders it. Shared partials (header, footer,
  topic rows, errors) therefore link only to 16000-origin routes. Actions that only exist on
  one page COME FROM that page's line: PLEASE, quote, edit, delete and moderation from the topic
  page (130), the editor's own submits from (140), sign-in from (150), registration from (160),
  promotion from the profile (170), the bio form from (180). `POST /t/:id/reply` is 16000
  because both the topic page and the editor post to it.
- **Validation re-renders answer 200.** A form template can only be rendered from its one
  line, so it cannot also be rendered with a 422 status from a second line. The message is in
  the page (`role="alert"`). Hard failures (404/403/400) use `error.html`, which links only to
  16000 routes and is rendered from one line per status.
- **Per-row permissions in the template.** Templates compare row values with program
  variables (`,143 IS :100`, `.102` moderator flag). "Author or moderator" is written as two
  mutually exclusive ABSTAIN blocks including the same partial.
- **"Did I PLEASE this post?"** cannot come from a view (it depends on the reader), so forum.i
  fills `;100 SUB k` per post row with a `HOW MANY` query and the template reads
  `;100 SUB .144`.
- **Numbers arrive as digits.** Template extraction turns numbers into decimal tail arrays, so
  flags are compared with `IS #0`/`IS #1` (the bytes `"0"` are truthy), counts print with
  `IN ROMAN` and carry the arabic value in a `title` tooltip. Zero, which has no Roman
  numeral, renders as `_`, the way READ OUT spells it. Count labels agree with the number
  (`I post`, `II posts`) via `{{ABSTAIN FROM ,n IS #1}}s{{REINSTATE}}`.
- **Case-insensitive names** come from `users.name ... COLLATE NOCASE`: equality, uniqueness
  and profile lookup all ignore case without INTERCAL lowercasing anything.
- **Search** uses one `haystack` column (title + body for opening posts, body for replies)
  because ActiveCassette clauses are ANDed. Results link to `/p/:id`, the permalink route that
  computes the post's page with syslib and redirects to `/t/:id?page=N#pID`. Search pagination
  uses GET forms so the browser URL-encodes the query; templates have no URL-encoding filter.
- **Quote** pre-fills the editor from two template parameters (author, body); the template
  writes the `[quote="…"]` block, so forum.i never concatenates strings.
- **Soft delete** keeps the row; views blank the body of deleted posts so it cannot leak
  through a template. Deleting a post also forgets its PLEASEs.
- **You cannot PLEASE your own post** ("that would be overly polite"); the button is not shown
  and the route answers 403.
- **Locking** blocks replies and quoting for everyone, moderators included (unlock first).
  Editing, deleting and PLEASE still work in locked topics.

## Visual design

Navy top bar and footer with a green rule, the Interstate-72 shield as the logo, section
headers drawn as green guide signs (white inset border, condensed capitals), green primary
buttons with the same inset line, yellow pinned markers, coloured category badges. System
fonts; code in monospace with ligatures off so `<-` stays `<-`. Light and dark follow
`prefers-color-scheme`. Layout is mobile-first: at 390 px the header is two rows (brand and
account, then nav and search), category and topic rows stack their counts, and post bodies use
the full card width. Focus is a 3 px highway-yellow outline; every icon has a text
alternative; navigation uses `aria-current`, the PLEASE button `aria-pressed`. The page
works without JavaScript and without inline scripts (the runtime's CSP forbids them).

Copy: "PLEASE"/"PLEASED" for like, "GIVE UP" for sign out (with a "Sign out" tooltip),
"COME FROM here" above the reply box, "ABSTAINED FROM" for deleted posts, locked topics and
empty states, errors as `E404 NO SUCH TOPIC. PLEASE CORRECT SOURCE AND RESUBNIT`.

## Previews

`preview/*.html` were rendered from `templates/` with the seed database by a throwaway mock of
the SPEC §4 renderer (extraction, loops, ABSTAIN blocks, filters) and the ActiveCassette
queries exactly as ROUTES.md specifies them; the generator is not part of the repository.
Faces, `AGO` and prose in the previews are approximations of the runtime's output. Open
them with any static server rooted at `forum/` (they link `../public/forum.css`).

Screenshots in `preview/shots/` (`<page>-<desktop|mobile>-<light|dark>.webp`): board index,
topic and compose in all four combinations; latest, category, moderator topic view, locked
topic, quote-reply, profile, search, sign in, register and error at desktop/light and
mobile/dark.

## Validation against the real compiler

`forum.i` declares all 27 routes with the origins in ROUTES.md and renders every template from
its assigned line; `build/ick -s -b` compiles it with the static link check and the politeness
check passing, and `tests/run.sh` exercises every route. Rendering `topic.html`,
`compose.html`, `profile.html` or `login.html` from a wrong line fails with
`ICL401I NO ROAD LEADS TO …`, so the check is live.

## Contract notes for other components

- Every view has its row id in column #0; forum.i uses `HAVING #0 x` on views and the
  primary-key lookup shape only on tables.
- Whole-table counts are written `HAVING #0 BEYOND #0 HOW MANY`, so a query with only a shape
  keyword never has to be told apart from a primary-key lookup.
- Templates rely on: filters accepting decimal-digit bytes (`{{,142 IN ROMAN}}`, `AGO`),
  nested ABSTAIN blocks whose contents are not evaluated when skipped, `IS` between digit bytes
  and numbers, and the link checker ignoring `?query` and `#fragment`.
