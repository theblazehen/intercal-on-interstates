# PLEASE DISCUSS — the INTERCAL on Interstates community forum

A forum written in INTERCAL on Interstates (`forum.i`, compiled with `ick -s -b`), served by
`gross`. Quality bar: between phpBB and Discourse — phpBB's board structure and moderation,
Discourse's clean reading experience. It must be a forum people would actually use, and it must
be unmistakably INTERCAL.

## Rule zero
Forum logic lives in INTERCAL: routing, authentication checks, authorisation, validation,
pagination arithmetic (via the system library), and every database read and write. SQL lives
only in `schema.sql`/`seed.sql` (tables, indexes, read-only views for joins and aggregates).
No JavaScript is required for any feature; a small progressive-enhancement script is allowed.

## Features
1. **Board index** (`/`): categories grouped in sections, each with description, topic and post
   counts (Roman numerals, arabic in a `title` tooltip), and the latest post (topic, author, time).
2. **Latest** (`/latest`): Discourse-style list across all categories by last activity, with
   category badge, author faces of recent posters, reply count, last activity time.
3. **Category** (`/c/:id`, `?page=N`): topics, pinned first then by last activity; 20 per page;
   locked and pinned markers; "New topic" for signed-in users.
4. **Topic** (`/t/:id`, `?page=N`): posts in order, 20 per page; each post shows the author card
   (face, name, role badge, join date, post count), rendered prose, time, permalink anchor
   `#p<id>`, PLEASE count (likes) with a PLEASE button, Quote, Edit (own post or moderator,
   shows "edited"), Delete (own post or moderator; soft delete shows a tombstone). Reply form at
   the bottom unless locked. Topic view counter.
5. **Compose**: new topic (category, title, body) and reply; server-side Preview button that
   re-renders the form with the rendered prose. Validation errors re-render the form with the
   user's text intact and a clear message (empty title/body, title > 120 bytes,
   body > 20 000 bytes).
6. **Prose**: markdown subset (paragraphs, bold, italic, inline and fenced code, links,
   autolinks, lists, headings, block quotes), phpBB `[quote="name"]…[/quote]`, `@mentions`
   linking to profiles. Quote button pre-fills the reply with a `[quote]` block.
7. **Accounts**: register (username 3–20 of `[A-Za-z0-9_-]`, unique case-insensitively, password
   ≥ 8 bytes, confirmation), sign in, sign out (POST), profile page `/u/:name` (face, role,
   joined, post count, PLEASEs received, bio as prose, recent posts), edit own bio.
8. **Roles**: member, moderator, admin. The first registered account becomes admin. Moderators
   pin/unpin, lock/unlock, move topics between categories, delete any post. Admin can promote/
   demote moderators from the user's profile.
9. **Search** (`/search?q=`): topics by title and posts by body, newest first, paginated, with
   the matching post linked.
10. **Seed data**: four categories across two sections — e.g. "Announcements", "COME FROM
    Considered Helpful" (help), "Show & Tell", "ABSTAINed (off-topic)" — with a dozen topics and
    believable INTERCAL-flavoured posts, so the forum looks alive on first run. Seed accounts
    have known passwords listed in `forum/README.md`.
11. **Errors**: 404/403/500 pages and validation failures stay in voice ("E404 NO SUCH TOPIC.
    PLEASE CORRECT SOURCE AND RESUBNIT").

## Experience
- Server-rendered, fast, responsive down to 360 px, light and dark (`prefers-color-scheme`),
  keyboard accessible, semantic HTML, visible focus, sensible `<title>` per page.
- Visual identity: nods to the original site (navy navbar, the INTERCAL on Interstates wordmark,
  highway-sign green accents) without Bootstrap 2 — hand-written CSS in `public/forum.css`.
- Copy is playful INTERCAL ("PLEASE" for like, "GIVE UP" for sign out, "COME FROM" for reply
  links) but every control's purpose stays obvious.

## Acceptance
- `mise run forum` builds and serves on :10585 from a fresh checkout; a fresh database seeds.
- An end-to-end script (`forum/tests/run.sh`, curl-driven) covers: register, first-user admin,
  sign in/out, create topic, reply, quote, edit, delete, PLEASE/un-PLEASE, pin/lock/move,
  locked topic rejects replies, permission denials (editing someone else's post → 403),
  CSRF rejection without Origin, pagination boundaries, search, validation errors, XSS payloads
  rendered inert.
- A browser pass (screenshots, desktop and 390 px mobile, light and dark) shows a forum someone
  would want to post in.
