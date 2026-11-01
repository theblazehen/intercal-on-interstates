#!/bin/sh
# tests/forum/run.sh — `make check` entry for the PLEASE DISCUSS end-to-end test.
exec sh "$(dirname "$0")/../../forum/tests/run.sh" "$@"
