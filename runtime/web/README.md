# runtime/web — the Interstates web runtime

Everything a compiled `ick -s` program needs to answer a CGI request: request
parsing, routing, fields, sessions, the buffered response, `AS PROSE`, `AGO`,
`AS FACE`, error pages and ActiveCassette over the vendored SQLite. The public
contract is `include/ioi_web.h`; `web_internal.h` holds helpers shared with
`tests/web`. No dependencies beyond libc and `vendor/sqlite`.

| File | Contents |
|---|---|
| `request.c` | CGI environment, urlencoded decoding, cookies, routing, fields, digit words, same-origin check |
| `session.c` | signed session cookie |
| `response.c` | response buffer, CGI output, redirects, error pages, `AGO`, `AS FACE` |
| `prose.c` | `AS PROSE` markdown subset |
| `crypto.c` | SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256, password hash format |
| `db.c` | ActiveCassette |
| `util.c` | growable buffer, HTML escaping |

Tests: `tests/web/run.sh` (C unit tests in `tests/web/unit.c`, a hand-written
CGI in `tests/web/app/notes.c` served by `gross`, and gross tests).

## Request

- A request is present when `REQUEST_METHOD` is set. `PATH_INFO` is taken as
  already percent-decoded (CGI/1.1; gross does this); `QUERY_STRING` is raw.
- The body is read from stdin only when `CONTENT_TYPE` is
  `application/x-www-form-urlencoded`, at most `CONTENT_LENGTH` bytes and at
  most 1 MiB. Other bodies (multipart, JSON) are left unread.
- Field lookup: route parameter, then form body, then query string. The first
  occurrence of a repeated name wins. `+` and `%XX` are decoded; malformed
  escapes are kept literally.
- Numbers: decimal digits, or C-INTERCAL's full `WRITE IN` digit-word table
  (English incl. `OH` and `NINER`, plus Sanskrit, Basque, Tagalog, Nahuatl,
  Georgian, Kwak'wala, Volapük, Latin) — matched case-insensitively, unlike
  C-INTERCAL, because forms are typed by people. Values above 4294967295, signs
  and mixed digits/words are invalid (0). Range checks for `.n` are the
  caller's.

## Routing

`[METHOD ]/seg/:name/*`. METHOD is upper-case letters, default `GET`, and
`GET` also answers `HEAD`. Empty segments are ignored on both sides, so a
trailing slash does not matter. `:name` matches one non-empty segment; `*` is
only legal as the last segment and matches the rest (zero or more segments,
joined by `/`). Parameters are BYTES in pattern order. The bare pattern `*` is
the catch-all and answers every method; `POST *` answers POST only. A
malformed pattern never matches (`ioi_web_match` reports -1).

## Same-origin check

`Origin` is used when present (`null` fails); otherwise `Referer`. Its
`host[:port]` (scheme http or https, default port dropped, case-insensitive,
userinfo ignored) must equal `HTTP_HOST` (default port dropped for the same
scheme). No header at all fails. When `IOI_HOSTS` (comma-separated
`host` or `host:port`; a bare host allows any port) is set, an `HTTP_HOST`
not on it fails too — a DNS-rebinding page can't pass by sending its own
`Host` and `Origin`. gross enforces the same list for every request (421).

## Sessions

Server-side. The cookie `ioi_session=<id>.<mac>` carries a random 128-bit
id (32 hex digits from `/dev/urandom`) and HMAC-SHA256(`IOI_SECRET`, id),
checked in constant time. The number lives in the framework table
`ioi_sessions(key, value, expires)` under the hex SHA-256 of the id, so a
copy of the database holds no usable cookies.

- Setting a value issues a fresh id (and deletes the old row); setting 0
  deletes the row and sends `Max-Age=0`. A copied or stolen cookie therefore
  dies at logout, and a reset database (new `cassette.db`) honours no old
  cookie, even though user ids restart.
- Rows expire 30 days after they were set; expired rows are purged whenever
  a session is set. Attributes `Path=/; HttpOnly; SameSite=Lax;
  Max-Age=2592000`. With `IOI_SECURE_COOKIES=1` (or `HTTPS=on`) the cookie
  is `__Host-ioi_session=…; Secure` — set it when a TLS terminator sits in
  front of gross; gross passes `IOI_*` through.
- Without `IOI_SECRET` the session is always 0 and `ioi_web_session_set`
  returns -1; it also returns -1 when the database can't store the row.
  Sessions need the database even for programs that use no tables.
- `IOI_NOW` overrides the clock (tests).

## Response

Buffered; `ioi_web_finish` writes `Status`, `Content-Type`, `Location`,
`Set-Cookie`, `Content-Length` and these fixed headers:

```
Cache-Control: private, no-cache
X-Content-Type-Options: nosniff
X-Frame-Options: DENY
Referrer-Policy: same-origin
Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline';
  img-src 'self' data:; form-action 'self'; frame-ancestors 'none'; base-uri 'none'
```

So pages may use inline styles but scripts must come from `/public/`. HEAD
answers carry the headers (with the GET length) and no body.

- Redirects: the template rendering appended since `ioi_web_mark()` becomes
  the `Location` — trimmed, with the five entities `{{ }}` escaping produces
  decoded back, CR/LF dropped and other control/non-ASCII bytes
  percent-encoded — and the status becomes 303.
- Error pages: `ioi_web_error_page(status, text)` throws away the body and
  sends a small self-contained page with the `ICLnnnI` text in a `<pre>`. The
  texts are the caller's (see `compiler/README.md`).

## AS PROSE

Markdown subset, rendered to HTML in which every byte of input is escaped
or replaced by a tag this code writes. There is no raw HTML passthrough.

| Input | Output |
|---|---|
| blank-line separated text | `<p>`; a single newline inside a paragraph is `<br>` |
| `**b**`, `*i*` | `<strong>`, `<em>` (`_` is always literal, so `snake_case` is safe) |
| `` `code` `` (any backtick run length) | `<code>` |
| ```` ```lang ```` … ```` ``` ```` | `<pre><code class="language-lang">` |
| `[text](url)` | `<a href="url" rel="nofollow ugc">` |
| `<https://…>`, bare `http(s)://…` | autolink; trailing `.,:;!?'*]` and unbalanced `)` stay outside |
| `- `, `* `, `+ ` / `1. `, `1) ` | `<ul>` / `<ol>` (`start="n"` when not 1); nest by indenting ≥ 2 spaces; blank lines between items make `<p>` items |
| `#`…`######` | `<h3>`…`<h6>` (shifted by two so posts never outrank the page; deeper levels stay `h6`) |
| `> ` | `<blockquote>` |
| `[quote]`, `[quote=name]`, `[quote="name"]` … `[/quote]` | `<blockquote class="bbquote"><cite><a href="/u/name">name</a> wrote:</cite>…` — nests; the name is linked only when it is `[A-Za-z0-9_-]{1,32}`; unmatched tags stay literal |
| `@name` | `<a class="mention" href="/u/name">@name</a>`, name `[A-Za-z0-9_-]{1,32}` (not after a letter, digit, `_`, `-` or `.`, so e-mail addresses are left alone) |
| `\*` etc. | literal punctuation |

URLs are accepted only with scheme `http`/`https` (followed by `//host`) or no
scheme at all (relative, `/…`, `#…`, `?…`); a scheme is whatever precedes the
first `:` that comes before any `/ ? #`. URLs containing whitespace, control
characters, `"`, `<`, `>`, `` ` `` or `\` are rejected and the source text is
shown instead. Lists do not take lazy continuation lines (a line without
indentation after a list ends it), and a list may interrupt a paragraph
(ordered lists only when they start at 1). Nesting of quotes/lists/block
quotes is capped at 16 levels and emphasis/link nesting at 16; deeper input is
rendered as text. `[quote]` tags are recognised anywhere outside fenced code,
including inside inline code spans; openers and closers are paired once per
block level with a stack (linear time — 280 KB of unclosed `[quote]` renders
in well under a second).

## AGO and AS FACE

`AGO` accepts a number of Unix seconds or BYTES holding decimal seconds,
`YYYY-MM-DD`, `YYYY-MM-DD HH:MM:SS` or `YYYY-MM-DDTHH:MM:SSZ` (UTC) and
writes plain text: `just now` (< 60 s), `N minute(s)/hour(s)/day(s)/month(s)
(30 d)/year(s) (365 d) ago`, or `in N …` for the future. Anything else is
written escaped as-is.

`AS FACE` hashes the lower-cased name with SHA-256 into a mirrored 5×5 grid
and a hue: `<svg class="ick-face" viewBox="0 0 5 5" width="48" height="48"
role="img"><title>name</title>…</svg>`, well under 1 KB. Size it with CSS.

## ActiveCassette

- Opened lazily on first use: `IOI_DATABASE` (default `./cassette.db`),
  5 s busy timeout, `journal_mode=WAL`, `foreign_keys=ON`. The WAL is not
  checkpointed when a request's process closes the database (that cost two
  fsyncs, ~25 ms, per writing request); SQLite checkpoints it as it grows,
  so `cassette.db-wal` normally stays next to the database.
- Set-up: inside one `BEGIN IMMEDIATE` transaction, a database without
  application objects runs `IOI_APP_DIR/schema.sql` then `seed.sql` (each
  optional; default directory is that of the executable), creates
  `ioi_sessions`, then hashes every PASSWORD value not already in hash format.
  A process killed part-way leaves the database untouched and the next
  request does it all again. So `schema.sql`/`seed.sql` must not contain
  `BEGIN`/`COMMIT`, and seeds may carry plaintext passwords. An initialised
  database is opened without taking the write lock.
- Names starting with `ioi_` are reserved for the framework.
- Tables: `sqlite_master` rows of type `table` or `view` in `rowid` (=
  creation) order, from 0, skipping `sqlite_*`, `ioi_*` and virtual-table
  shadow tables. Columns: `pragma_table_info` order, from 0.
- Values out: INTEGER in 0..4294967295 → number (`ioi_val_number`), other
  integers and REAL → decimal BYTES, TEXT/BLOB → BYTES, NULL → empty BYTES.
- Values in: numbers bind as INTEGER, BYTES as TEXT. In `HAVING` (equality and
  `BEYOND`) and the primary-key lookup, BYTES that are a canonical decimal
  (`"7"`, not `"07"`) bind as INTEGER, so route parameters match integer keys
  and aggregate view columns alike; TEXT columns still compare as text by
  SQLite affinity. A LIST operand is `IOI_DB_E_VALUE`.
- SQL is `"col" = ?`, `"col" LIKE ? ESCAPE '\'`, `"col" > ?` — columns are
  never wrapped, so declared collations (e.g. `COLLATE NOCASE`) apply.
  `SOMEWHERE` is `%value%` with `%`, `_` and `\` escaped (ASCII
  case-insensitive, SQLite's `LIKE`). Equality with the empty string also
  matches NULL, since NULL reads back as the empty string.
- `PASSWORD` columns (declared type exactly `PASSWORD`, any case) store
  `pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>`: 16-byte salt from
  `/dev/urandom`, 32-byte PBKDF2-HMAC-SHA256, 80000 iterations (about 30 ms
  per hash here: after the first round each iteration is two SHA-256
  compressions on pre-padded blocks; a sign-in is ~40 ms end to end and a
  fresh forum database with 10 seed users sets up in ~0.3 s; the format
  records the count, so it can be raised later without breaking old hashes).
  Every insert or update hashes. `HAVING #c expr` on a PASSWORD column
  verifies the candidate in constant time; those conditions are applied in C
  after the SQL conditions, with `SKIP`/`TAKE` applied after them. When no
  row reaches that check (unknown user), one verification against a fixed
  dummy hash runs anyway, so a missing account answers as slowly as a wrong
  password. `SOMEWHERE`/`BEYOND` on a PASSWORD column is `IOI_DB_E_VALUE`.
  Because the declared type has NUMERIC affinity, an all-digit plaintext
  seed password loses leading zeros before it is hashed: don't seed such
  passwords.
- `SKIP`/`TAKE`/`BY` on update and delete select the target rows by `rowid`;
  so do PASSWORD conditions there. Tables declared `WITHOUT ROWID` support
  only unpaged updates/deletes without PASSWORD conditions.
- Status codes: `IOI_DB_E_OPEN`, `_TABLE`, `_COLUMN`, `_VALUE`, `_SQL`, with
  detail from `ioi_db_errmsg()`.
