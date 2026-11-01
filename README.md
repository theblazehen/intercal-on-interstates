# INTERCAL on Interstates 1.1

*A web framework for the modern developer.* In 2013, [intercaloninterstates.org](https://www.intercaloninterstates.org/)
announced a web framework for INTERCAL, and its 1.0 download contained one file:
`APRIL FOOL'S! :)` ([reference/](reference/)). Version 1.1 ships the framework the site
described, with every example on its examples page compiling unmodified, and a community forum
written in it.

| Piece | What it is |
|---|---|
| `ick` | INTERCAL → C → native compiler. Without flags it matches C-INTERCAL 0.35 (143/143 differential tests). `-s` enables the Interstates extensions: routes via `COME FROM … WITH ROUTE`, compiled templates, `WRITE IN … WITH FIELD`, sessions, and ActiveCassette. |
| `gross` | General READ OUT Server System, an HTTP/1.1 server on port 10585 that runs your program once per request as CGI. |
| ActiveCassette | Numbered tables over SQLite. `'FROM DATABASE #1~#3$#4' HAVING #0 ,3` is a query; `STASH ;5 INTO DATABASE #2` is an insert; `FORGET` deletes. |
| [`forum/`](forum/) | **PLEASE DISCUSS**, a forum somewhere between phpBB and Discourse, written in about 1,100 lines of INTERCAL. |

The full language and framework contract is in [SPEC.md](SPEC.md). Each component has its own
README: [compiler](compiler/README.md), [web runtime](runtime/web/README.md),
[gross](gross/README.md), [forum](forum/README.md), [site examples](examples/site/README.md).

## Install

All you need is a C compiler.

```sh
./configure
make
make install
```

The installed footprint is about 2.4 MB, including SQLite.

## First steps

```sh
ick -s -b hello.i      # -s: Interstates; -b: turn off the random compiler bug (E774)
gross ./hello          # http://localhost:10585/
```

## The forum

```sh
mise run forum         # build, compile forum/forum.i, serve on :10585
```

The seed accounts are listed in [forum/README.md](forum/README.md).

## Development

A nix dev shell provides the toolchain and C-INTERCAL as the conformance oracle
(`nix develop`). `make check` runs every suite in `tests/*/run.sh`: conformance against
C-INTERCAL, the site examples, the web runtime and gross, and the forum end to end.

## Licence

`lib/syslib.i` and `lib/floatlib.i` come from C-INTERCAL and are GPL; see
[lib/COPYING](lib/COPYING). SQLite is public domain.
