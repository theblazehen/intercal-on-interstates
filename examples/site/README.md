# The four examples of reference/examples.html

`hello.i`, `routing.i`, `database.i` and `blog.i` are the four code blocks of the 2013 site,
copied verbatim. The rest is what they imply:

- `templates/hello.html`, `home.html`, `db_sample.html` — written for the examples;
  `templates/blog_post.html` is the template shown on the site, verbatim.
- `schema.sql` — table #0 `greetings` (column #3 is the greeting `database.i` shows) and
  table #1 `posts` (columns #3 title, #4 date, #5 text, as `blog.i` selects them).
- `seed.sql` — two greetings and two posts. `public/site.css` — served at `/public/`.

```
build/ick -s -b examples/site/hello.i          # and the others
cd examples/site && ../../build/gross ./hello  # http://localhost:10585/
```

`routing.i` serves `/home` and `/blog/post/:id`; the latter COMEs FROM `(16)`, the line that
renders `home.html`, so it only answers requests whose Referer/Origin is this site (follow
the link on `/home`). `tests/site/run.sh` checks all four.
