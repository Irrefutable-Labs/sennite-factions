#!/bin/zsh
# The two-player check (Source/Sennite/Private/Tests/NetTest.cpp): one game hosts, one joins.
# The joining player gets a hero, sees the host's, builds a wall the server keeps and charges for,
# walks, and sees the host walk; the host sees all of that from its side.
# Logs and screenshots in Saved/Net/.
exec "$(dirname "$0")/run_two_player_check.sh" Sennite.Net Net
