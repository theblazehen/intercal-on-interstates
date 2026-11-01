-- Schema implied by the INTERCAL on Interstates examples.
-- Table #0: greetings (example "Database access": row #1, column #3).
CREATE TABLE greetings (
    id INTEGER PRIMARY KEY,
    language TEXT NOT NULL,
    script TEXT NOT NULL,
    greeting TEXT NOT NULL
);
-- Table #1: blog posts (columns #3 title, #4 date, #5 text).
CREATE TABLE posts (
    id INTEGER PRIMARY KEY,
    author TEXT NOT NULL,
    slug TEXT NOT NULL,
    title TEXT NOT NULL,
    posted TEXT NOT NULL,
    body TEXT NOT NULL
);
