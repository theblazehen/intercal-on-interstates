-- PLEASE DISCUSS — schema.
--
-- ActiveCassette numbers tables AND views by creation order (indexes and
-- sqlite_* do not count) and columns by declaration order. The numbers in
-- the comments below are the contract used by forum.i; forum/ROUTES.md lists
-- them again. Never reorder, insert or drop anything here without updating
-- ROUTES.md and forum.i: every number after the change shifts.
--
-- Times are Unix seconds (INTEGER). Flags are INTEGER 0/1 and NOT NULL.
-- Views only join and aggregate; they never decide anything. Every view
-- column is NOT NULL in practice (COALESCE), so templates never see a
-- NULL-turned-empty-bytes where they expect a number.

-- #0 users
--   #0 id  #1 name  #2 password  #3 role  #4 joined  #5 bio
-- role: 1 member, 2 moderator, 3 admin.
-- name uses COLLATE NOCASE, so `HAVING #1 x` is case-insensitive, which is
-- also what makes the UNIQUE constraint case-insensitive.
CREATE TABLE users (
  id       INTEGER PRIMARY KEY,
  name     TEXT    NOT NULL UNIQUE COLLATE NOCASE,
  password PASSWORD NOT NULL,
  role     INTEGER NOT NULL DEFAULT 1 CHECK (role IN (1, 2, 3)),
  joined   INTEGER NOT NULL,
  bio      TEXT    NOT NULL DEFAULT ''
);

-- #1 sections (board index groups)
--   #0 id  #1 name  #2 position
CREATE TABLE sections (
  id       INTEGER PRIMARY KEY,
  name     TEXT    NOT NULL,
  position INTEGER NOT NULL DEFAULT 0
);

-- #2 categories
--   #0 id  #1 section_id  #2 name  #3 description  #4 color  #5 position
-- color is a CSS colour literal ("#0b6e4f"), used for the category badge.
CREATE TABLE categories (
  id          INTEGER PRIMARY KEY,
  section_id  INTEGER NOT NULL REFERENCES sections(id),
  name        TEXT    NOT NULL,
  description TEXT    NOT NULL DEFAULT '',
  color       TEXT    NOT NULL DEFAULT '#0b6e4f',
  position    INTEGER NOT NULL DEFAULT 0
);

-- #3 topics
--   #0 id  #1 category_id  #2 user_id  #3 title  #4 created  #5 bumped
--   #6 pinned  #7 locked  #8 views
-- bumped = time of the last reply (forum.i sets it on create and on reply).
CREATE TABLE topics (
  id          INTEGER PRIMARY KEY,
  category_id INTEGER NOT NULL REFERENCES categories(id),
  user_id     INTEGER NOT NULL REFERENCES users(id),
  title       TEXT    NOT NULL,
  created     INTEGER NOT NULL,
  bumped      INTEGER NOT NULL,
  pinned      INTEGER NOT NULL DEFAULT 0 CHECK (pinned IN (0, 1)),
  locked      INTEGER NOT NULL DEFAULT 0 CHECK (locked IN (0, 1)),
  views       INTEGER NOT NULL DEFAULT 0
);

-- #4 posts
--   #0 id  #1 topic_id  #2 user_id  #3 body  #4 created  #5 edited  #6 deleted
-- edited: 0 = never, else Unix time of the last edit.
-- deleted: 0 = live, 1 = deleted by its author, 2 = deleted by a moderator.
-- Deleted posts stay (tombstones); the views blank their body.
CREATE TABLE posts (
  id       INTEGER PRIMARY KEY,
  topic_id INTEGER NOT NULL REFERENCES topics(id),
  user_id  INTEGER NOT NULL REFERENCES users(id),
  body     TEXT    NOT NULL,
  created  INTEGER NOT NULL,
  edited   INTEGER NOT NULL DEFAULT 0,
  deleted  INTEGER NOT NULL DEFAULT 0 CHECK (deleted IN (0, 1, 2))
);

-- #5 pleases (likes)
--   #0 id  #1 post_id  #2 user_id  #3 created
CREATE TABLE pleases (
  id      INTEGER PRIMARY KEY,
  post_id INTEGER NOT NULL REFERENCES posts(id),
  user_id INTEGER NOT NULL REFERENCES users(id),
  created INTEGER NOT NULL,
  UNIQUE (post_id, user_id)
);

CREATE INDEX categories_by_section ON categories(section_id, position, id);
CREATE INDEX topics_by_category    ON topics(category_id, pinned, bumped);
CREATE INDEX topics_by_bumped      ON topics(bumped);
CREATE INDEX topics_by_user        ON topics(user_id);
CREATE INDEX posts_by_topic        ON posts(topic_id, id);
CREATE INDEX posts_by_user         ON posts(user_id, id);
CREATE INDEX pleases_by_user       ON pleases(user_id);

-- #6 board_rows — one row per category, for the board index and for
-- category pickers. Order with `BY #3 BY #0`; first_in_section is 1 on the
-- first category of each section in exactly that order.
--   #0 id               #1 section_id      #2 section_name   #3 sort_key
--   #4 first_in_section #5 name            #6 description    #7 color
--   #8 topic_count      #9 post_count      #10 last_post_id  #11 last_topic_id
--   #12 last_topic_title #13 last_poster   #14 last_post_at
-- last_* describe the newest live post in the category (0 / '' when none).
CREATE VIEW board_rows AS
SELECT
  c.id                                                     AS id,
  c.section_id                                             AS section_id,
  s.name                                                   AS section_name,
  s.position * 1000000 + s.id * 1000 + c.position          AS sort_key,
  CASE WHEN EXISTS (
         SELECT 1 FROM categories c2
          WHERE c2.section_id = c.section_id
            AND (c2.position < c.position
                 OR (c2.position = c.position AND c2.id < c.id)))
       THEN 0 ELSE 1 END                                   AS first_in_section,
  c.name                                                   AS name,
  c.description                                            AS description,
  c.color                                                  AS color,
  (SELECT COUNT(*) FROM topics t WHERE t.category_id = c.id) AS topic_count,
  (SELECT COUNT(*) FROM posts p JOIN topics t ON t.id = p.topic_id
    WHERE t.category_id = c.id AND p.deleted = 0)          AS post_count,
  COALESCE(lp.id, 0)                                       AS last_post_id,
  COALESCE(lp.topic_id, 0)                                 AS last_topic_id,
  COALESCE(lt.title, '')                                   AS last_topic_title,
  COALESCE(lu.name, '')                                    AS last_poster,
  COALESCE(lp.created, 0)                                  AS last_post_at
FROM categories c
JOIN sections s ON s.id = c.section_id
LEFT JOIN posts lp ON lp.id = (
  SELECT p.id FROM posts p JOIN topics t ON t.id = p.topic_id
   WHERE t.category_id = c.id AND p.deleted = 0
   ORDER BY p.created DESC, p.id DESC LIMIT 1)
LEFT JOIN topics lt ON lt.id = lp.topic_id
LEFT JOIN users  lu ON lu.id = lp.user_id;

-- #7 topic_rows — one row per topic, for /latest, /c/:id and the topic header.
--   #0 id            #1 category_id   #2 category_name  #3 category_color
--   #4 title         #5 user_id       #6 author         #7 created
--   #8 bumped        #9 pinned        #10 locked        #11 views
--   #12 reply_count  #13 post_count   #14 last_post_id  #15 last_poster
--   #16 recent1      #17 recent2      #18 recent3       #19 recent4
--   #20 first_post_id
-- post_count counts live posts; reply_count = live posts minus the opening
-- post (never negative). recent1..4: distinct live posters other than the
-- author, most recent first ('' when fewer).
CREATE VIEW topic_rows AS
SELECT
  t.id                                                     AS id,
  t.category_id                                            AS category_id,
  c.name                                                   AS category_name,
  c.color                                                  AS category_color,
  t.title                                                  AS title,
  t.user_id                                                AS user_id,
  u.name                                                   AS author,
  t.created                                                AS created,
  t.bumped                                                 AS bumped,
  t.pinned                                                 AS pinned,
  t.locked                                                 AS locked,
  t.views                                                  AS views,
  MAX((SELECT COUNT(*) FROM posts p WHERE p.topic_id = t.id AND p.deleted = 0) - 1, 0)
                                                           AS reply_count,
  (SELECT COUNT(*) FROM posts p WHERE p.topic_id = t.id AND p.deleted = 0)
                                                           AS post_count,
  COALESCE((SELECT p.id FROM posts p WHERE p.topic_id = t.id AND p.deleted = 0
             ORDER BY p.created DESC, p.id DESC LIMIT 1), 0)               AS last_post_id,
  COALESCE((SELECT u2.name FROM posts p JOIN users u2 ON u2.id = p.user_id
             WHERE p.topic_id = t.id AND p.deleted = 0
             ORDER BY p.created DESC, p.id DESC LIMIT 1), '')              AS last_poster,
  COALESCE((SELECT u2.name FROM posts p JOIN users u2 ON u2.id = p.user_id
             WHERE p.topic_id = t.id AND p.deleted = 0 AND p.user_id <> t.user_id
             GROUP BY p.user_id ORDER BY MAX(p.created) DESC, MAX(p.id) DESC LIMIT 1 OFFSET 0), '') AS recent1,
  COALESCE((SELECT u2.name FROM posts p JOIN users u2 ON u2.id = p.user_id
             WHERE p.topic_id = t.id AND p.deleted = 0 AND p.user_id <> t.user_id
             GROUP BY p.user_id ORDER BY MAX(p.created) DESC, MAX(p.id) DESC LIMIT 1 OFFSET 1), '') AS recent2,
  COALESCE((SELECT u2.name FROM posts p JOIN users u2 ON u2.id = p.user_id
             WHERE p.topic_id = t.id AND p.deleted = 0 AND p.user_id <> t.user_id
             GROUP BY p.user_id ORDER BY MAX(p.created) DESC, MAX(p.id) DESC LIMIT 1 OFFSET 2), '') AS recent3,
  COALESCE((SELECT u2.name FROM posts p JOIN users u2 ON u2.id = p.user_id
             WHERE p.topic_id = t.id AND p.deleted = 0 AND p.user_id <> t.user_id
             GROUP BY p.user_id ORDER BY MAX(p.created) DESC, MAX(p.id) DESC LIMIT 1 OFFSET 3), '') AS recent4,
  COALESCE((SELECT MIN(p.id) FROM posts p WHERE p.topic_id = t.id), 0)
                                                           AS first_post_id
FROM topics t
JOIN categories c ON c.id = t.category_id
JOIN users u      ON u.id = t.user_id;

-- #8 post_rows — one row per post (deleted ones included, body blanked),
-- for the topic page, quoting, editing and permalinks.
--   #0 id           #1 topic_id      #2 user_id       #3 author
--   #4 author_role  #5 role_label    #6 author_joined #7 author_posts
--   #8 body         #9 created       #10 edited       #11 deleted
--   #12 please_count #13 number      #14 topic_title  #15 topic_locked
--   #16 category_id
-- number = 1-based position in the topic counting every post, deleted or
-- not, so it matches the topic page order and pagination (20 per page).
CREATE VIEW post_rows AS
SELECT
  p.id                                                     AS id,
  p.topic_id                                               AS topic_id,
  p.user_id                                                AS user_id,
  u.name                                                   AS author,
  u.role                                                   AS author_role,
  CASE u.role WHEN 3 THEN 'admin' WHEN 2 THEN 'moderator' ELSE 'member' END
                                                           AS role_label,
  u.joined                                                 AS author_joined,
  (SELECT COUNT(*) FROM posts p2 WHERE p2.user_id = p.user_id AND p2.deleted = 0)
                                                           AS author_posts,
  CASE WHEN p.deleted = 0 THEN p.body ELSE '' END          AS body,
  p.created                                                AS created,
  p.edited                                                 AS edited,
  p.deleted                                                AS deleted,
  (SELECT COUNT(*) FROM pleases l WHERE l.post_id = p.id)  AS please_count,
  (SELECT COUNT(*) FROM posts p3 WHERE p3.topic_id = p.topic_id AND p3.id <= p.id)
                                                           AS number,
  t.title                                                  AS topic_title,
  t.locked                                                 AS topic_locked,
  t.category_id                                            AS category_id
FROM posts p
JOIN users u  ON u.id = p.user_id
JOIN topics t ON t.id = p.topic_id;

-- #9 user_rows — profile header and stats.
--   #0 id  #1 name  #2 role  #3 role_label  #4 joined  #5 bio
--   #6 post_count  #7 topic_count  #8 pleases_received  #9 pleases_given
--   #10 last_post_at
CREATE VIEW user_rows AS
SELECT
  u.id                                                     AS id,
  u.name                                                   AS name,
  u.role                                                   AS role,
  CASE u.role WHEN 3 THEN 'admin' WHEN 2 THEN 'moderator' ELSE 'member' END
                                                           AS role_label,
  u.joined                                                 AS joined,
  u.bio                                                    AS bio,
  (SELECT COUNT(*) FROM posts p WHERE p.user_id = u.id AND p.deleted = 0)
                                                           AS post_count,
  (SELECT COUNT(*) FROM topics t WHERE t.user_id = u.id)   AS topic_count,
  (SELECT COUNT(*) FROM pleases l JOIN posts p ON p.id = l.post_id
    WHERE p.user_id = u.id)                                AS pleases_received,
  (SELECT COUNT(*) FROM pleases l WHERE l.user_id = u.id)  AS pleases_given,
  COALESCE((SELECT MAX(p.created) FROM posts p
             WHERE p.user_id = u.id AND p.deleted = 0), 0) AS last_post_at
FROM users u;

-- #10 post_index — search results and profile "recent posts".
--   #0 id           #1 topic_id      #2 title          #3 user_id
--   #4 author       #5 created       #6 excerpt        #7 deleted
--   #8 category_id  #9 category_name #10 category_color #11 number
--   #12 is_first    #13 haystack
-- haystack = title + body for a topic's opening post, body alone for
-- replies, so one `HAVING #13 SOMEWHERE q` finds topics by title and posts
-- by body. Always add `HAVING #7 #0` (live posts only). excerpt is the first
-- 280 bytes of the raw markdown on one line, minus ** and backticks, with
-- [quote="x"]…[/quote] spelled x wrote: “…” (rendered escaped, not as prose).
CREATE VIEW post_index AS
SELECT
  p.id                                                     AS id,
  p.topic_id                                               AS topic_id,
  t.title                                                  AS title,
  p.user_id                                                AS user_id,
  u.name                                                   AS author,
  p.created                                                AS created,
  CASE WHEN p.deleted = 0
       THEN replace(replace(replace(replace(replace(replace(replace(replace(
              substr(p.body, 1, 280),
              char(13), ''), char(10), ' '), '**', ''), '```', ''), '`', ''),
              '[quote="', ''), '"]', ' wrote: “'), '[/quote]', '” ')
       ELSE '' END                                         AS excerpt,
  p.deleted                                                AS deleted,
  t.category_id                                            AS category_id,
  c.name                                                   AS category_name,
  c.color                                                  AS category_color,
  (SELECT COUNT(*) FROM posts p3 WHERE p3.topic_id = p.topic_id AND p3.id <= p.id)
                                                           AS number,
  CASE WHEN p.id = (SELECT MIN(p4.id) FROM posts p4 WHERE p4.topic_id = p.topic_id)
       THEN 1 ELSE 0 END                                   AS is_first,
  CASE WHEN p.deleted <> 0 THEN ''
       WHEN p.id = (SELECT MIN(p4.id) FROM posts p4 WHERE p4.topic_id = p.topic_id)
       THEN t.title || char(10) || p.body
       ELSE p.body END                                     AS haystack
FROM posts p
JOIN topics t     ON t.id = p.topic_id
JOIN categories c ON c.id = t.category_id
JOIN users u      ON u.id = p.user_id;

-- #11 forum_stats — exactly one row, id 1. Board index footer.
--   #0 id  #1 users  #2 topics  #3 posts  #4 pleases  #5 newest_member
CREATE VIEW forum_stats AS
SELECT
  1                                                        AS id,
  (SELECT COUNT(*) FROM users)                             AS users,
  (SELECT COUNT(*) FROM topics)                            AS topics,
  (SELECT COUNT(*) FROM posts WHERE deleted = 0)           AS posts,
  (SELECT COUNT(*) FROM pleases)                           AS pleases,
  COALESCE((SELECT name FROM users ORDER BY joined DESC, id DESC LIMIT 1), '')
                                                           AS newest_member;
