#!/bin/zsh
# The combat check (Source/Sennite/Private/Tests/CombatTest.cpp): one game hosts, one joins.
# The joining player shoots the host twice, for 25 each; the host shoots the joining player until
# they fall, in four shots, and is credited with the kill; the fallen player becomes a spectator and
# leaves a crate holding their weapons where they fell.
# Health, rounds, the hit marker, the hurt flash, kills and players left are checked on both sides.
# Logs and screenshots in Saved/Combat/.
exec "$(dirname "$0")/run_two_player_check.sh" Sennite.Combat Combat
