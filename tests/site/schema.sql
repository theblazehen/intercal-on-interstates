-- Kitchen-sink guestbook. Tables are numbered in creation order.
-- #0 users: #1 name, #2 password (PASSWORD: hashed on write, verified by HAVING), #3 joined
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    password PASSWORD NOT NULL,
    joined INTEGER NOT NULL
);
-- #1 entries: #1 author, #2 message, #3 created (unix seconds), #4 likes
CREATE TABLE entries (
    id INTEGER PRIMARY KEY,
    author TEXT NOT NULL,
    message TEXT NOT NULL,
    created INTEGER NOT NULL,
    likes INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX entries_created ON entries (created);
-- #2 stats (a view): #1 number of entries, #2 total likes
CREATE VIEW stats AS
    SELECT 1 AS id, count(*) AS entries, coalesce(sum(likes), 0) AS likes FROM entries;
