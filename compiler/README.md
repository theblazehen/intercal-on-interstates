# ick — the INTERCAL on Interstates compiler

`ick` compiles INTERCAL to C and then to a native executable with `$CC` (default `cc`).
Without `-s` it is a drop-in for C-INTERCAL 0.35's `ick` on INTERCAL-72 plus `COME FROM`;
with `-s` it compiles the Interstates dialect of `SPEC.md` §2–§4.

```
ick [-b] [-s] [-c] [-o FILE] program.i
  -b   no random compiler bug (E774); every build in this repo passes it
  -s   INTERCAL on Interstates
  -c   keep the generated program.c
  -o   executable name (default: the source name without .i, next to it)
```

`-O`, `-f`, `-F` and `-w` (C-INTERCAL optimiser/warning flags) are accepted and ignored.

`ick` finds `include/`, `lib/libioi.a` and `share/` (`syslib.i`, `floatlib.i`) beside its
own executable (`build/ick` → `build/include` …), or under `../include/ioi`, `../lib/ioi`,
`../share/ioi` for an installed prefix. `IOI_HOME` overrides the directory to search (either
layout). The C command is `$CC -std=gnu11 -O1 -w -I<include> -o OUT prog.c <lib>/libioi.a -lm`.

## Architecture

| File | Role |
|---|---|
| `compiler/lex.c` | Re-implementation of C-INTERCAL's `lexer.l`: every flex rule is simulated as a small NFA, so the token is flex's longest match (earliest rule on ties) and the scanner reads exactly as far ahead as flex would. `iyylineno` (the line numbers in error messages) is "1 + newlines read so far", as in C-INTERCAL's `getc()`. Also the spark/ears classification stack and the politeness counter. `source_load` performs `getc()`'s rewriting (`!` → `'.`, backspace → `^H`, CR stripping, final newline). |
| `compiler/parse.c` | Recursive-descent parser that accepts exactly the language of C-INTERCAL's yacc grammar (`parser.y`), with the same `%prec` conflict resolutions, the same yacc error recovery (a bad statement becomes a "splattered" statement and parsing resumes at the next `DO`/`PLEASE`/`MAYBE`/label token; junk after a valid statement splatters that statement; a label not followed by `DO` splatters the previous one) and side effects at the moments yacc performs its reductions (E017 for constants over 65535, E200 for variable 0, E197 for bad labels, the `stbeginline` line bookkeeping). In `-s` mode each line is a statement and the Interstates statements are added. |
| `compiler/gen.c` | C-INTERCAL's `codecheck` (E182, E129, E139, E444, E555, emit-time E405/E111/E997) and C generation. |
| `compiler/tpl.c` | Templates: loading, parsing, static link checking, C generation. |
| `compiler/main.c` | Driver: options, politeness check, magic library inclusion, `$CC`. |
| `compiler/util.c` | Compile-time error messages, allocation helpers. |
| `runtime/core/runtime.c` | Error messages/exit statuses, mingle/select/unary operators, assignment with IGNORE, arrays, STASH/RETRIEVE, the 80-deep NEXT stack, READ OUT (butchered Roman numerals), WRITE IN (digit words in every language C-INTERCAL knows), Turing Text array I/O — all line for line from C-INTERCAL's `cesspool.c`/`fiddle.c`. |
| `runtime/core/values.c` | Interstates value conversions (§2.2) and template rendering helpers (§4). |
| `runtime/core/interstates.c` | Glue for `-s` programs: error pages, response flushing, URL security, ActiveCassette status checks. |
| `include/ioi_core.h` | The generated-code ↔ core-runtime boundary. |

### Generated code

The program is one `for (;;) switch (pc)` loop. Statements fall through to each other;
only labelled statements get a `case`. `NEXT` pushes a return case and sets `pc`; `RESUME`
pops it; a COME FROM suck point sits after its target statement and jumps to the COME
FROM's landing case. Each statement is `ioi_lineno = N;` (the "ON THE WAY TO" line), an
abstention/chance guard and one or two calls into `libioi` — expressions are calls such as
`ioi_mingle(V1[3], 7u)`. Variables are static arrays indexed by interned number; abstentions
are one byte per statement. A 3000-statement program compiles (ick + `cc -O1`) in about 3 s.

Exactly as in C-INTERCAL: falling off the end of the program is E129 (not E633); the random
bug (without `-b`, a 1-in-10 chance per compilation) is an E774 placed after one randomly
chosen statement; `%n` chances use `rand()` seeded with the time; the error text of E000 is
the source lines from the statement up to the next statement (C-INTERCAL's `nice_text`,
including its 512-byte truncation); errors print `ICLnnnI\t…` to stderr after flushing
stdout and exit with the error number (mod 256).

### Libraries

When a program `NEXT`s to a label in 5000–5699 (floatlib) or 1000–1999 (syslib) and defines
no label in that range, `ick` appends `share/floatlib.i` and/or `share/syslib.i` (copied
verbatim from C-INTERCAL, GPL — see `lib/COPYING`), floatlib first, numbering their lines
after the program's own, exactly as C-INTERCAL does. The politeness check happens before.

## C-INTERCAL compatibility

Supported: everything of INTERCAL-72, `COME FROM`, the Latin keywords, all mingle/XOR
spellings, `ONCE`/`AGAIN` (statements toggle their own abstention — this matters because
comments ending in `ONCE` or `AGAIN` carry the flag), unknown statements ("just in case"
compilation: they die with E000 when executed), and the libraries above.

Deviations (all features outside INTERCAL-72 + COME FROM):

- Computed `COME FROM`, `NEXT FROM`, computed `ABSTAIN expr FROM`, `CREATE`, `TRY AGAIN`,
  operand overloading (`/`) and `PIN` parse exactly as in C-INTERCAL but are rejected with
  `ICL111I COMMUNIST PLOT DETECTED, COMPILER IS SUICIDING` (C-INTERCAL's error for these
  under `-t`). Inside a comment (an unknown or splattered statement) they are harmless.
- `MAYBE`, `GO BACK`, `GO AHEAD` and `WHILE` give E405, as C-INTERCAL does without `-m`.
  `|`, `^` and `@` give E997, as C-INTERCAL does in base 2.
- Programs in other bases (`.3i` … `.7i`) are compiled as base 2; C-INTERCAL's many other
  command-line options (`-m`, `-e`, `-a`, `-C`, `-y`, …) do not exist.
- When the C compiler fails, `ick` prints the command and E778.

## Interstates (`-s`)

### Lexical rules

- Each non-blank line is one statement. After an optional label, a line that does not
  start with `DO`, `PLEASE` (`PLEASE DO`) or `MAYBE` gets an implied `DO`; `NOTE …` is
  therefore `DO NOT E…`, a comment. Politeness counts every non-blank line as a statement
  and every line whose preamble is `PLEASE` as polite. A line that does not parse is a
  splattered statement: it dies with E000 if it is ever executed, as in INTERCAL.
  `ONCE`/`AGAIN` may end a line.
- Interstates keywords are whole upper-case words: `WITH WITHOUT ROUTE TEMPLATE REDIRECT
  SESSION STATUS FIELD TIME INTO DATABASE HAVING SOMEWHERE BEYOND BACKWARDS SKIP TAKE IN BULK
  HOW MANY IS AS ARABIC ROMAN PROSE AGO FACE`.
- **String literals (the disambiguation rule).** After `WITH ROUTE`, `WITH TEMPLATE`,
  `WITH REDIRECT`, `WITH FIELD` and in `{{DO "file" NEXT}}`, `"…"` is always a string. In an
  operand position, a `"` is first parsed as an opening rabbit-ear grouping; if no
  well-formed grouping (contents + closing `"`) results, it is instead a string literal that
  runs to the next unescaped `"` on the line. Escapes: `\"` and `\\`. So `"?.1$.2"~#1` is a
  grouping, while `"Hello, world!"`, `"12"` (no `#`) and `"V"` are strings. A string whose
  contents happen to be a valid expression (`"#1"`, `".1~.2"`) cannot be written in an
  operand position; put it in a template or build it another way.
- Labels and syntax: `(16) READ OUT …` (a label without `DO`) is allowed.

### Statements added by `-s`

```
COME FROM (L) WITH ROUTE "[METHOD ]/path/:param/*"     route; bare COME FROM (16000) = "/"
READ OUT expr WITH TEMPLATE "file" [WITH STATUS #n]
READ OUT expr WITH REDIRECT "file"
READ OUT expr WITH SESSION
WRITE IN x WITH FIELD "name"      x: ,n (bytes)  .n :n (number)  ;n SUB k (BYTES)  ;n (LIST of one BYTES)
WRITE IN x WITH SESSION           x: scalar or array element
WRITE IN x WITH TIME
STASH lvalue <- expr              assignment; a one-dimensional array grows to fit SUB k
STASH ;n INTO DATABASE #t         insert ;n SUB k into column k; :4400 <- row id
'FROM DATABASE #t~SEL' CLAUSE… <- expr          update
FORGET 'FROM DATABASE #t' CLAUSE…               delete
```

Expressions gain string literals, whole arrays as values (`,n` → BYTES, `;n` → LIST), the
DB read `'FROM DATABASE #t~SEL' CLAUSE…` (sparks or ears) and `value SUB k` on LIST values.
Clauses attach to the DB group and are parsed greedily, so to subscript a filtered query
wrap it once more: `"'FROM DATABASE #1~#3' HAVING #0 ,3" SUB #1`. A whole-array operand of
`HAVING` must not be followed by `SUB` (it would be read as an element). With clauses, SEL
must be `#c` or a `$`-chain of constants (else E413); without clauses SEL is any expression
(the primary key). `IN BULK` and `HOW MANY` may each appear once.

Gerund classes: route `COME FROM`s are COMING FROM; `READ OUT … WITH …` is READING OUT;
`WRITE IN … WITH …` is WRITING IN; STASH-assign and `INTO DATABASE` are STASHING; a DB
update is CALCULATING; a DB `FORGET` is FORGETTING. A route whose COME FROM is initially
abstained (`DON'T COME FROM …`) is not served.

### Values

Numbers stored into a hybrid element are boxed (`ioi_val_number`) when ≥ 2³¹. In
arithmetic and scalar assignment a value is converted with `ioi_num`: BYTES of decimal
digits are parsed (E410 otherwise), LIST is E411. A cassette assigned to a tail element is
E412; BYTES assigned to a whole hybrid array is E414. Assigning a plain number to a whole
array dimensions it as usual (`#0` is still E240); assigning BYTES fills a tail array (length
0 allowed). `ioi_val_equal`/`ioi_val_truthy` implement `IS` and conditions.

### Routing and URL security

At start, when `ioi_web_active()`, the route COME FROMs are tried in source order (catch-all
`"*"` last); the first match fills `;4400` (if the program mentions it) and execution
continues after that COME FROM. No match → E404 page (HTTP 404). A route whose origin is not
16000, and every non-GET/HEAD request, must pass `ioi_web_same_origin()`, else E403 (HTTP
403). Run from a shell, the program runs top-down and route COME FROMs are no-ops.

**Origin rule** (compile time): `L` must be 16000 or the label of a `READ OUT … WITH
TEMPLATE` statement (E400).

**Static link checking** (compile time): for every `READ OUT … WITH TEMPLATE` at line L (with
label L, or none), all `href`, `action` and `formaction` values of the template and its
includes that start with `/` (not `//`, not `/public/`) must match a route whose origin is
16000 or L. The value's `?query` and `#fragment` are ignored; path segments are compared one
by one: static segments literally, segments containing `{{…}}` only against `:param`
segments, a final `*` matches the rest. `<form method="post">` actions (and `formaction`s
inside them, unless `formmethod` says otherwise) need `POST` routes; everything else `GET`.
The catch-all `"*"` route never satisfies a link (it is the 404 handler). The trimmed static
text of a `WITH REDIRECT` template is checked as a GET link by the same rule.

### Templates

Looked up in `templates/` next to the source, then next to the source; compiled into the
program as one C function each. Tags: `{{EXPR [FILTER]}}`, `{{DO "file" NEXT}}`,
`{{DO EXPR NEXT}}…{{RESUME}}`, `{{ABSTAIN FROM COND}}…{{REINSTATE}}`,
`{{ABSTAIN WITHOUT COND}}…{{REINSTATE}}`, `{{NOTE …}}`; `COND` is `EXPR` or `EXPR IS EXPR`.
Blocks nest; tags inside a skipped block are not evaluated. Filters: `IN ARABIC`,
`IN ROMAN`, `AS IS`, `AS PROSE`, `AGO`, `AS FACE`. `IN ARABIC`/`IN ROMAN` on BYTES of digits
parse them (other BYTES are shown escaped). Numbers render as Roman numerals (HTML, overbars
as `<span class="ick-bar">…</span>`, lowercase letters for ×1 000 000); zero renders as `_`,
READ OUT's own spelling. The compiler tracks where each `{{…}}` sits in the HTML: inside an
attribute value (quoted or not), an HTML comment, or a `<title>`, `<textarea>`, `<script>`
or `<style>` element, a Roman numeral is plain text instead, each barred letter followed by
U+0305 COMBINING OVERLINE (`I̅V̅`), so no markup is ever emitted there. Each template starts
in element content (an include does not inherit its includer's context).
Bad-character operators are not allowed in templates (E406).
Extraction and loops follow SPEC.md §4; a loop saves `,1…,144`, `;1…;144` and `.144` and
restores them afterwards.

## Error codes

Compile-time errors in `-s` mode (and template errors) are prefixed with `file:line: `.

| Code | Text | When |
|---|---|---|
| E400 | YOU CAN ONLY COME FROM A PAGE | route origin is not 16000 or a `READ OUT … WITH TEMPLATE` label |
| E401 | NO ROAD LEADS TO *method link* FROM *template* | static link check failed |
| E402 | THERE IS NO TEMPLATE CALLED *name* IN THE CLOSET | template file not found |
| E403 | YOU CAN'T GET THERE FROM HERE | (runtime, HTTP 403) origin/Referer check failed |
| E404 | I'M ALL OUT OF CHOICES! | (runtime, HTTP 404) no route matched |
| E406 | THE TEMPLATE IS GRUMPY: *detail* | template syntax error |
| E407 | THAT ROUTE IS NOT ON ANY MAP: *pattern* | malformed route pattern |
| E408 | A TEMPLATE MAY NOT BE ITS OWN GRANDFATHER: *name* | recursive template include |
| E409 | YOU CANNOT WRITE THAT IN THERE | `WRITE IN` a whole array `WITH SESSION`/`WITH TIME` |
| E410 | THAT IS NOT A NUMBER, HOWEVER YOU SPELL IT | (runtime) BYTES used as a number are not decimal |
| E411 | A WHOLE CASSETTE WILL NOT FIT IN ONE SPOT | (runtime) LIST used as a number/string, or loop over a non-list |
| E412 | A TAIL HOLDS BYTES, NOT CASSETTES | (runtime) cassette stored into a tail element |
| E413 | THE COLUMNS OF A QUERY MUST BE CONSTANTS | DB query with clauses and a non-constant SEL |
| E414 | BYTES DO NOT MAKE A LIST | (runtime) BYTES assigned to a whole hybrid array |
| E420 | THE CASSETTE IS CHEWED UP: *detail* | (runtime) database failure |
| E421 | NO SUCH SHELF IN THE CASSETTE LIBRARY: *detail* | (runtime) no such table/column |
| E422 | THE SESSION WOULD NOT STAY PUT | (runtime) `READ OUT … WITH SESSION` could not store the session while serving (no `IOI_SECRET`, or the session table refused it) |

Runtime errors in a web request render the error page (status 403/404 as above, else 500)
with the full `ICLnnnI` text, still print it to stderr, and exit with the error number.

## Tests

- `tests/conformance/run.sh` — differential against `$ORACLE_ICK -b`: 30+2 pit programs
  (fft and flonck use floatlib) and ~110 hand-written edge cases; `SLOW=1` adds four more.
- `tests/site/run.sh` — the four examples of `reference/examples.html` (`examples/site/`) and
  the kitchen-sink guestbook `tests/site/kitchen_sink.i` (every Interstates construct),
  compiled with `ick -s -b`, served by `gross`, checked with curl; plus compile-error cases.
