# gross — the General READ OUT Server System

```
gross [-p PORT] [-d PUBLIC_DIR] [-b ADDRESS] [-t SECONDS] [-c N] [-l SECONDS] ./prog
```

| Option | Default | |
|---|---|---|
| `-p` | 10585 | listen port |
| `-d` | `./public` | served at `/public/...` |
| `-b` | all interfaces (`::` dual-stack, else `0.0.0.0`) | bind address (extra; tests use `127.0.0.1`) |
| `-t` | 10 | per-request time limit in seconds for the body and the program (extra; tests use 2) |
| `-c` | 16 | connections at once from one client address; 0 = no limit (extra). Behind a reverse proxy every client shares the proxy's address: raise it or use 0 |
| `-l` | 60 | connection lifetime in seconds: no new request starts on an older keep-alive connection (extra) |
| `IOI_HOSTS` | unset | comma-separated `host` / `host:port` allow-list for the `Host` header (a bare host allows any port); others get 421. Also passed to the program, whose same-origin check applies it |

One process is forked per connection; it serves requests on that connection
with HTTP/1.1 keep-alive and pipelining. The first byte and the whole request
head must arrive within 5 s (or `-t`, if shorter) whatever `-t` says; idle
keep-alive connections are closed after 5 s, after 1000 requests, or once
they are older than `-l`. HTTP/1.0 closes unless the client asks for
keep-alive. At most 256 connections at once (beyond that: `503`) and `-c`
per client address (beyond that: `429`), both with `Retry-After: 1`.
SIGINT/SIGTERM stop the server and its connection processes.

## Requests

- `/public/...` (GET/HEAD only, else 405): the decoded path must not contain
  a `..` segment (`/` and `\` both separate), and its `realpath` must lie
  inside `realpath(PUBLIC_DIR)` — so symlinks pointing outside are refused.
  Only regular files; directories and misses are 404. `Content-Type` by
  extension (html, css, js, json, svg, png, jpg, gif, webp, ico, woff2, …;
  otherwise `application/octet-stream`), `Last-Modified`, `If-Modified-Since`
  → 304, `Cache-Control: no-cache`.
- Everything else runs `./prog` once as CGI/1.1, in gross's working
  directory, with a clean environment: `REQUEST_METHOD`, `PATH_INFO`
  (percent-decoded; `%00` and malformed escapes are 400), `QUERY_STRING`
  (raw), `CONTENT_TYPE`, `CONTENT_LENGTH` (when there is a body),
  `REQUEST_URI`, `SCRIPT_NAME` (empty), `SERVER_NAME/PORT/PROTOCOL/SOFTWARE`,
  `GATEWAY_INTERFACE`, `REMOTE_ADDR/PORT`, every request header as `HTTP_*`
  (repeats joined with `, `, cookies with `; `; `Proxy` and names containing
  `_` are dropped), `PATH`, `TZ`, any `IOI_*` from gross's own environment,
  and:
  - `IOI_SECRET` — 32 random bytes as hex, created once in `./.gross_secret`
    (mode 0600; looser modes are tightened on start).
  - `IOI_DATABASE` — gross's `$IOI_DATABASE`, else `$PWD/cassette.db`.
  - `IOI_APP_DIR` — gross's `$IOI_APP_DIR`, else the program's directory.

  The body arrives on stdin (from an unlinked temporary file). The program's
  stdout must be a CGI response: header lines, a blank line (`\n` or
  `\r\n`), the body. `Status:` sets the status (default 200, or 302 with a
  `Location`); `Content-Type` defaults to `text/html; charset=utf-8`; gross
  owns `Content-Length`, `Connection` and `Transfer-Encoding`. Stderr goes to
  gross's stderr.

## Limits and failures

| Status | When | Text |
|---|---|---|
| 400 | malformed request line or header, bad escapes, missing `Host` on 1.1, conflicting `Content-Length`, `Content-Length` with `Transfer-Encoding`, a chunk-size line that is not exactly 1–8 hex digits (optionally `;extension`) and CRLF, chunk data not followed by CRLF | `ICL017I DO YOU EXPECT ME TO FIGURE THIS OUT?` |
| 408 | request head not complete within 5 s (or `-t`), or body within `-t` | `ICL408I YOU TOOK TOO LONG TO SAY THAT` |
| 413 | body over 1 MiB (refused before reading when `Content-Length` says so; chunked bodies are checked chunk by chunk without overflow) | `ICL413I THAT BODY IS TOO HEAVY TO WRITE IN` |
| 421 | `IOI_HOSTS` is set and the `Host` header is not on it | `ICL421I THIS SERVER DOES NOT ANSWER TO THAT NAME` |
| 429 | more than `-c` connections from one address | `ICL429I YOU ARE ALREADY COMING FROM HERE TOO MANY TIMES` |
| 431 | request head over 16 KiB or over 100 headers | `ICL431I YOUR HEADERS ARE LONGER THAN MY PATIENCE` |
| 500 | the program could not be started | `ICL500I THE PROGRAM REFUSED TO BE STARTED` |
| 501 | a transfer coding other than `chunked` | `ICL501I I DO NOT KNOW HOW TO UNPACK THAT` |
| 502 | output without a header block, bad header, or over 64 MiB | `ICL502I THE PROGRAM ANSWERED IN A LANGUAGE NOBODY SPEAKS` |
| 504 | the program still running when the time limit (from the first request byte) expires: its process group is killed | `ICL504I THE PROGRAM WOULD NOT GIVE UP, SO WE GAVE UP ON IT` |
| 505 | `HTTP/` other than 1.x | `ICL505I THAT IS NOT A DIALECT OF HTTP I SPEAK` |

These numbers are HTTP statuses wearing ICL clothing; they are gross's own and
independent of the language's E-codes. After an error response the connection
is half-closed and the rest of the request drained for up to 2 s, so clients
read the answer instead of a reset. `Expect: 100-continue` is honoured.

## Log

One line per request on stderr, written with a single `write`:

```
127.0.0.1 - - [01/Nov/2026:09:06:58 +0000] "GET / HTTP/1.1" 200 155 4.0ms
```
