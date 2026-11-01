# INTERCAL on Interstates 1.1 — Specification

The 2013 site (reference/examples.html) was an April Fools' joke; the tarball held only
`APRIL FOOL'S! :)`. This project makes every claim on that site true. Where the site's
examples are inconsistent, this document is the tie-breaker. **The four site examples must
compile unmodified and serve the pages they describe** (see §8).

Components and owners:

| Component | Path | What |
|---|---|---|
| `ick` | `compiler/` | INTERCAL → C → native compiler. `-s` enables Interstates. |
| core runtime | `runtime/core/` | variables, operators, NEXT/STASH/ABSTAIN machinery, errors, Roman numerals, template rendering helpers, cassettes |
| web runtime | `runtime/web/` | CGI request, routing match, fields, sessions, ActiveCassette (SQLite), prose (markdown), escaping, faces |
| `gross` | `gross/` | General READ OUT Server System: HTTP server, default port 10585 |
| shared contracts | `include/ioi_cassette.h` (frozen), `include/ioi_web.h` (web runtime owner), `include/ioi_core.h` (compiler owner) |

Everything links into `build/lib/libioi.a` (core + web + vendored SQLite). `ick` emits C, then
runs `$CC` (default `cc`) on it with `-I<include dir> <libioi.a> -lm`. `ick` finds its include dir,
library and `syslib.i` relative to its own executable: `../include ../lib ../share` for the build
tree (`build/ick` → `build/include`, `build/lib/libioi.a`, `build/share/syslib.i`) and
`../include/ioi ../lib/ioi ../share/ioi` for an installed prefix. `IOI_HOME` overrides.
Dependency budget: a C compiler and libc. Nothing else at build or run time.

---

## 1. Base language

`ick` without `-s` is a C-INTERCAL-compatible compiler for INTERCAL-72 plus `COME FROM`.
The reference is C-INTERCAL 0.35 (`$ORACLE_ICK`, manual at `info ick` / `$ORACLE_SHARE`).
Required:

- Variables `.n` (16-bit), `:n` (32-bit), `,n` (16-bit arrays), `;n` (32-bit arrays), multi-dim `SUB`.
- Constants `#n` (0–65535), mingle `$` (C-INTERCAL also accepts `¢`), select `~`, unary `&` `V` `?`
  placed after the variable's type char (`.&1`, `#V3`, `'?.1$.2'`). Sparks `'` and rabbit-ears `"`
  group with C-INTERCAL's disambiguation rules.
- Statements: assignment `<-` (array assignment of a number = dimensioning), `NEXT`, `RESUME`,
  `FORGET`, `STASH`, `RETRIEVE`, `IGNORE`, `REMEMBER`, `ABSTAIN FROM` (labels and gerund lists),
  `REINSTATE`, `GIVE UP`, `READ OUT`, `WRITE IN` (numbers spelled in English digit words; arrays via
  Turing Text Model), `COME FROM`, `DO`/`PLEASE`/`PLEASE DO`, `NOT`/`N'T`, `%n` chance, labels `(n)`.
- READ OUT numbers as butchered Roman numerals, byte-for-byte as C-INTERCAL prints them.
- NEXT stack depth 80 (E123), politeness check (E079 too few PLEASEs < 1/5, E099 too many > 1/3,
  using C-INTERCAL's integer rule), unrecognisable statements are runtime E000 only if executed.
- C-INTERCAL error message texts and numbers, printed as `ICLnnnI <text>` then
  `        ON THE WAY TO STATEMENT nnnn` / `CORRECT SOURCE AND RESUBNIT` as C-INTERCAL does.
- The random compiler bug (E774) is present and on by default, exactly like C-INTERCAL; `-b`
  disables it. Every build in this repo passes `-b`.
- System library: when a program references labels 1000–1999 it does not define, `ick` appends
  `syslib.i` (copied verbatim from C-INTERCAL, GPL — see `lib/COPYING`).
- Output: `ick prog.i` writes executable `prog` next to the source. `-o FILE` overrides,
  `-c` keeps the generated `prog.c`.

Conformance is differential: programs from C-INTERCAL's `pit/` (fetch the source with
`nix build nixpkgs#intercal.src`) and hand-written edge cases must produce identical stdout,
stderr text and exit status under `$ORACLE_ICK -b` and `build/ick -b`, given identical stdin.

## 2. Interstates dialect (`ick -s`)

### 2.1 Lexical
- **Boilerplate removal.** Each source line is one statement. A line not starting with a label,
  `DO`, or `PLEASE` gets an implied `DO`. Blank lines are ignored. (`NOTE ...` therefore parses as
  `DO NOT E...` — an abstained, never-executed statement, i.e. a comment. Statements counted for
  politeness are all non-blank lines.)
- **Rabbit-ear strings.** `"text"` where the parser expects a string operand (route, template
  name, field name, string expression) is an ASCII/UTF-8 string literal. In expression position a
  string literal evaluates to a BYTES cassette; assigned to a tail array it fills the array with
  bytes. Escapes: `\"`, `\\`. Inside expressions, rabbit-ears still group when the contents parse
  as an expression and not as a string (`"?.1$.2"~#1` stays a grouping); a string literal must
  contain at least one character that cannot appear in an INTERCAL expression, or appear in a
  string-operand position. Document the exact rule you implement in `compiler/README.md`.

### 2.2 Values
A hybrid (`;`) element is a `uint32_t` *value* per `include/ioi_cassette.h`: plain number below
2³¹, cassette handle (tagged) above. Interstates reserves the upper half of the hybrid address
space for cassettes. A tail (`,`) array holding a string is one-dimensional, one byte per element,
dimension = byte length (dimension 0 is legal in `-s`: the empty string).

Implicit conversions in `-s` mode:

| assign `<-` | from plain number | from BYTES cassette | from LIST cassette |
|---|---|---|---|
| `.n` / `:n` | as usual | parse decimal, E-error if not numeric or too big | E-error |
| `,n` (whole) | dimension (standard) | fill with bytes | E-error |
| `;n` (whole) | dimension (standard) | E-error | dimension to list length, copy elements (1-based) |
| `;n SUB k` | store | store handle | store handle |
| `,n SUB k` | store | E-error | E-error |

Reading a whole tail array `,n` in expression position (only legal in `-s` contexts that want a
value: hybrid-element assignment, DB operands, templates) yields a BYTES cassette of its bytes;
a never-dimensioned tail array reads as the empty string.
`expr SUB k` on a LIST value (e.g. a spark-grouped DB expression) indexes it 1-based.

**STASH-assign.** `STASH lvalue <- expr` is assignment that auto-extends a one-dimensional array
when the subscript exceeds its dimension (new elements 0). It does *not* push the stash.
Ordinary `STASH .1 + ,2` is unchanged.

### 2.3 Special variables
| Variable | Meaning |
|---|---|
| `;4400` | route parameters of the current request, BYTES cassettes in pattern order |
| `;49221` | template parameters (filled by the program before `READ OUT ... WITH TEMPLATE`) |
| `,144` `,143` … / `;144` … | template parameters extracted by the renderer (§4) |
| `.144` | 1-based iteration index inside a template loop |
| `.4401` | byte length of the last `WRITE IN ... WITH FIELD` (0 when absent) |
| `:4400` | row id produced by the last `STASH ... INTO DATABASE` |

### 2.4 Routing
- `COME FROM (L) WITH ROUTE "PATTERN"` declares a route. `PATTERN` is `[METHOD ]/path` with
  `:name` segments and an optional final `*` segment; METHOD defaults to `GET` (which also answers
  `HEAD`). The special pattern `"*"` is a catch-all.
- Bare `COME FROM (16000)` means `COME FROM (16000) WITH ROUTE "/"`.
- Label 16000 is the virtual statement "gross handed us a request". Many route COME FROMs may
  name 16000 or the same label; route COME FROMs are exempt from the one-COME-FROM-per-label rule.
- **Dispatch.** At program start, if a web request is present (`ioi_web_active()`), routes are
  matched in source order, catch-all last; the first match fills `;4400` and execution continues
  at the statement after that COME FROM. No match → 404 error page (§6). Without a request (run
  from a shell) the program runs top-down as plain INTERCAL. Route COME FROMs never fire from
  ordinary control flow.
- **Origin rule (compile time).** `L` must be 16000 or the label of a statement that
  `READ OUT ... WITH TEMPLATE`; otherwise compile error E…: `YOU CAN ONLY COME FROM A PAGE`.
- **Static link checking (compile time).** For a template rendered at line L (including its
  includes), every `href`/`action`/`formaction` attribute whose value starts with `/` is matched
  against the declared routes (static text before the first `{{` must match literally, dynamic
  parts match `:param` segments; query strings ignored; `form method="post"` matches `POST`
  routes, everything else `GET`). The link must match a route whose origin is 16000 or L.
  Violations are compile errors. Paths under `/public/` are static files and exempt.
- **URL security (run time).** A request to a route whose origin is not 16000, and every non-GET
  request, must carry an `Origin` or `Referer` header naming the same host, else 403
  (`YOU CAN'T GET THERE FROM HERE`). This is the framework's CSRF protection.

### 2.5 Request input
- `WRITE IN x WITH FIELD "name"` — reads a field. Lookup order: route param `:name`, form body
  (`application/x-www-form-urlencoded`), query string. `x` tail array → bytes; `.n`/`:n` → number
  (accepts decimal or C-INTERCAL digit words, `ONE TWO`; absent/invalid → 0). Sets `.4401`.
- `WRITE IN x WITH SESSION` / `READ OUT x WITH SESSION` — read / set the session value (a
  number; 0 when absent). Stored in an HMAC-SHA256-signed cookie keyed by `IOI_SECRET`.
  `READ OUT #0 WITH SESSION` logs out.
- `WRITE IN :n WITH TIME` — Unix time in seconds.

### 2.6 Response output
- `READ OUT expr WITH TEMPLATE "file" [WITH STATUS #n]` — sets template parameters count to
  `expr`, renders `file` (§4) into the response body. The first such statement fixes status
  (default 200) and `Content-Type: text/html; charset=utf-8` (`.txt` → text/plain,
  `.xml` → application/xml, `.json` → application/json). Later READ OUTs append.
- `READ OUT expr WITH REDIRECT "file"` — renders `file` and answers `303 See Other` with the
  trimmed rendering as `Location`.
- The response is buffered and sent at `GIVE UP` (or program end). A runtime error discards
  the buffer and renders the error page.
- Templates are found in `templates/` next to the source file, then next to the source file.

### 2.7 ActiveCassette (database)
Tables are numbered by their order of creation in the schema (`schema.sql` next to the
source): tables and views both count, `sqlite_*` and indexes don't. Columns are numbered by
declaration order; column #0 of a table is its `INTEGER PRIMARY KEY`. A column declared with type
`PASSWORD` stores a salted PBKDF2-SHA256 hash on write; `HAVING` on it verifies instead of
comparing. Text comes back as BYTES, integers as numbers, NULL as the empty BYTES.

Read expression (spark-grouped):

```
'FROM DATABASE #t~SEL' CLAUSE...
```
- No clauses: primary-key lookup. `SEL` is the row id; the value is a LIST of columns 1..N
  (the id is excluded, so `SUB #3` is column 3). Missing row → empty LIST.
- With clauses: `SEL` is `#c` or `#c$#c$…` (column list; in this position `$` chains left to
  right without grouping). Clauses, in any order:
  `HAVING #c expr` (equality), `HAVING #c SOMEWHERE expr` (substring, case-insensitive),
  `HAVING #c BEYOND expr` (greater than), `BY #c` / `BY #c BACKWARDS` (ordering, repeatable),
  `SKIP expr`, `TAKE expr`, and one shape keyword:
  - default: first matching row. One column selected → that value; several → LIST of them.
    No matching row → the empty LIST (test existence with `HOW MANY`).
  - `IN BULK` → LIST of rows, each shaped as above.
  - `HOW MANY` → number of matching rows (SEL ignored).

Writes:
- `STASH ;n INTO DATABASE #t` — insert; `;n SUB k` → column k. Sets `:4400` to the row id.
- `'FROM DATABASE #t~SEL' CLAUSE... <- expr` — update the selected columns of every matching row
  (one column: `expr` is the value; several: `expr` must be a LIST or whole hybrid array).
- `FORGET 'FROM DATABASE #t' CLAUSE...` — delete matching rows.

The database is `IOI_DATABASE` (default `./cassette.db` in the working directory). On first open
of an empty database the runtime runs `schema.sql` then `seed.sql` (if present) from
`IOI_APP_DIR` (default: directory of the executable) in one transaction. WAL mode,
5 s busy timeout. Every operand is bound as a parameter — no SQL text is built from data.

## 3. CGI contract (gross ↔ program)
`gross [-p PORT] [-d PUBLIC_DIR] ./prog` listens on PORT (10585), serves files under
`PUBLIC_DIR` (default `./public`) at `/public/...`, and runs `./prog` once per request as a
CGI/1.1 script: `REQUEST_METHOD`, `PATH_INFO`, `QUERY_STRING`, `CONTENT_TYPE`,
`CONTENT_LENGTH`, `HTTP_HOST`, `HTTP_COOKIE`, `HTTP_ORIGIN`, `HTTP_REFERER`, `REMOTE_ADDR`,
`IOI_SECRET` (generated once, persisted in `.gross_secret`, mode 0600), `IOI_DATABASE`,
`IOI_APP_DIR`; body on stdin. The program writes a CGI response (`Status:` + headers, blank line,
body). gross speaks HTTP/1.1 with keep-alive, caps request bodies at 1 MiB, kills a request after
10 s, logs one line per request, and survives malformed input. Because the program is plain CGI
it also runs under any other CGI server.

## 4. Templates
Compiled into the program by `ick` at build time (a template error is a compile error).
Text outside `{{ }}` is copied verbatim. Tags:

| Tag | Meaning |
|---|---|
| `{{EXPR}}` | render value, HTML-escaped (quotes too) |
| `{{EXPR FILTER}}` | filters: `IN ARABIC` (decimal), `IN ROMAN`, `AS IS` (raw), `AS PROSE` (markdown subset → sanitised HTML), `AGO` (relative time from Unix seconds or `YYYY-MM-DD HH:MM:SS` UTC), `AS FACE` (deterministic identicon SVG for a name) |
| `{{DO "file" NEXT}}` | include another template (same variable context) |
| `{{DO EXPR NEXT}} … {{RESUME}}` | loop over a LIST/hybrid array; each element is extracted (below) with `.144` = index; outer `,144…`/`;144…`/`.144` are restored afterwards |
| `{{ABSTAIN FROM COND}} … {{REINSTATE}}` | skip block when COND is truthy |
| `{{ABSTAIN WITHOUT COND}} … {{REINSTATE}}` | skip block when COND is falsy |
| `{{NOTE …}}` | comment |

`EXPR` is any INTERCAL rvalue (variables of the running program, constants, operators,
`SUB`, string literals). `COND` is `EXPR` or `EXPR IS EXPR` (`ioi_val_equal`); truthiness is
`ioi_val_truthy`. Rendering by kind: tail array / BYTES → text; `.n` `:n` / plain number →
Roman numerals exactly as READ OUT spells them, as HTML (overbar = `<span class="ick-bar">…</span>`,
the ×1 000 000 lowercase form likewise); LIST → elements rendered in order.

**Extraction.** Before rendering `READ OUT #n WITH TEMPLATE`, element k of `;49221`
(k = 1..n) goes to variable number 145−k: BYTES → `,145−k`; plain number → `,145−k` as its
decimal digits; LIST → `;145−k`. Loop iterations extract the element the same way: a LIST element
spreads over `,144`, `,143`, … (`;144`… for nested lists); a scalar element goes to `,144`.

## 5. C interfaces
- `include/ioi_cassette.h` — frozen, implemented in `runtime/core/cassette.c`.
- `include/ioi_web.h` — owned by the web runtime; generated code calls only what is declared
  there. Minimum surface: request init/active/method/path/same-origin check, route matching
  (`pattern` + params LIST out), field lookup, session get/set, time, response begin/status/
  content-type/append raw/append escaped/append prose/append ago/append face/redirect/finish,
  error page, and the ActiveCassette query struct with select/insert/update/delete.
- `include/ioi_core.h` — owned by the compiler; the generated-code ↔ core runtime boundary.

## 6. Errors
Runtime errors keep INTERCAL's style. In web mode the error page is status 500 (404 for no route,
403 for origin failures) with the `ICLnnnI` text in a `<pre>`. New Interstates errors use numbers
in the E400–E499 range and must read like the classics. Each new code and text is listed in
`compiler/README.md`.

## 7. Size and speed
Installed footprint under 3 MB (`du -sh` of the install prefix, excluding docs). A simple route
must answer in under 20 ms on loopback including process start.

## 8. Acceptance for the framework
1. `./configure && make && make install` into a temp prefix with only `cc`; installed `ick -s` and
   `gross` work from that prefix.
2. Differential suite vs C-INTERCAL passes (§1).
3. The four examples from reference/examples.html live verbatim in `examples/site/` with the
   templates, schema and seed data they imply; `ick -s -b` compiles each and `gross` serves the
   described page (`/`, `/home`, `/blog/post/1`, the DB examples) — checked by
   `tests/site/run.sh` with curl.
4. Every `tests/*/run.sh` passes via `make check`.
