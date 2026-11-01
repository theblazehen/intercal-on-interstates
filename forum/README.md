# PLEASE DISCUSS — forum design package

The data model, seed data, templates, styles and route contract for the INTERCAL on
Interstates community forum. The program itself (`forum.i`) is written against
[ROUTES.md](ROUTES.md); the product is described in [PRD.md](PRD.md).

| Path | What |
|---|---|
| `schema.sql` | 6 tables + 6 read-only views; numbering is the ActiveCassette contract |
| `seed.sql` | 10 accounts, 2 sections, 4 categories, 14 topics, 65 posts, 91 PLEASEs |
| `templates/` | 11 page templates, 5 partials (`_*.html`), 7 redirect templates (`to_*.txt`) |
| `public/forum.css` | all styling, hand-written, light + dark |
| `public/forum.js` | optional: delete confirmation, Ctrl/Cmd+Enter to post |
| `public/favicon.svg` | the Interstate-72 shield |
| `ROUTES.md` | every route, origin label, query, template parameter, message and redirect |
| `preview/*.html` | static pages rendered from the real templates and seed data |
| `preview/shots/` | screenshots of the previews: desktop 1280 px and mobile 390 px, light and dark |

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
  numeral, renders as `_`, the way READ OUT spells it.
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

A scratch program (outside the repository) declared all 27 routes with the origins in
ROUTES.md and rendered every template from its assigned line with the queries ROUTES.md
specifies. `build/ick -s -b` compiled it, static link check included, and its CGI output
against the seed database answered 200 for every page (404 for the catch-all) and 303 for
every redirect template. Rendering `topic.html`, `compose.html`, `profile.html` or `login.html`
from a wrong line fails with `ICL401I NO ROAD LEADS TO …`, so the check is live.

## Contract notes for other components

- Every view has its row id in column #0; forum.i uses `HAVING #0 x` on views and the
  primary-key lookup shape only on tables.
- Whole-table counts are written `HAVING #0 BEYOND #0 HOW MANY`, so a query with only a shape
  keyword never has to be told apart from a primary-key lookup.
- Templates rely on: filters accepting decimal-digit bytes (`{{,142 IN ROMAN}}`, `AGO`),
  nested ABSTAIN blocks whose contents are not evaluated when skipped, `IS` between digit bytes
  and numbers, and the link checker ignoring `?query` and `#fragment`.
