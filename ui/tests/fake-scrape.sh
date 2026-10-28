#!/bin/sh
# A stand-in for banana-scrape in the headless test (the game page's Box art, #28): two names for any search, and a
# --match written to $FAKE_SCRAPE_LOG
case $1 in
--search) printf 'NAME\tYellow Planet (USA)\nNAME\tYellow Planet - Second Orbit (Europe)\n' ;;
--match) printf '%s|%s|%s\n' "$2" "$3" "$4" >> "$FAKE_SCRAPE_LOG"; echo "OK cover title" ;;
*) exit 2 ;;
esac
