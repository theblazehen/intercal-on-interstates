#!/bin/sh
# Tiny CGI used by tests/web/run.sh to exercise gross on its own.
case "$PATH_INFO" in
/sleep)
    sleep 30
    ;;
/garbage)
    printf 'this is not a CGI response'
    ;;
/body)
    printf 'Status: 201 Created\r\nContent-Type: text/plain\r\nX-Test: yes\r\n\r\n'
    cat
    ;;
*)
    printf 'Content-Type: text/plain\n\n'
    printf 'method=%s\n' "$REQUEST_METHOD"
    printf 'path=%s\n' "$PATH_INFO"
    printf 'query=%s\n' "$QUERY_STRING"
    printf 'length=%s\n' "$CONTENT_LENGTH"
    printf 'host=%s\n' "$HTTP_HOST"
    printf 'cookie=%s\n' "$HTTP_COOKIE"
    printf 'remote=%s\n' "$REMOTE_ADDR"
    printf 'secret=%s\n' "${#IOI_SECRET}"
    printf 'database=%s\n' "$IOI_DATABASE"
    printf 'appdir=%s\n' "$IOI_APP_DIR"
    printf 'pid=%s\n' "$PPID"
    ;;
esac
