#!/bin/zsh
# Starts the publisher's match server for one match on the games host, where players' games join it at
# <host>:<port> (JOIN A LOBBY). It runs until the match ends (the game decides it and tells the publisher's server).
#
#   Tools/ovh_match.sh <match id> [factions, default 2] [port, default 7788]
#   Tools/ovh_match.sh --list          the match servers running
#   Tools/ovh_match.sh --stop <match id>
#
#   SENNITE_HOST (default 148.113.137.67), SSH_KEY (default ~/.ssh/dsm-deploy)
# Ports 7788-7797/udp are open on the host for match servers. The container shares the host's network because the
# publisher's server listens on loopback only; every other loopback service there (the DSM accounts' ingress, MySQL)
# needs a secret this container is not given. It gets the match secret alone, read-only, as the design asks.
set -euo pipefail
HOST="${SENNITE_HOST:-148.113.137.67}"
KEY="${SSH_KEY:-$HOME/.ssh/dsm-deploy}"
SSH=(ssh -o BatchMode=yes -i "$KEY" "ubuntu@$HOST")
case "${1:-}" in
  --list) "${SSH[@]}" 'sudo docker ps --filter label=sennite.match --format "{{.Label \"sennite.match\"}}  {{.Status}}  port {{.Label \"sennite.port\"}}"'; exit ;;
  --stop) "${SSH[@]}" "sudo docker rm -f sennite-match-${2:?match id}"; exit ;;
  ""|-*) sed -n 2,10p "$0"; exit 2 ;;
esac
MATCH="$1"; BASES="${2:-2}"; PORT="${3:-7788}"
[[ "$MATCH" =~ '^[A-Za-z0-9-]+$' ]] || { echo "a match id is letters, digits and dashes" >&2; exit 2; }
(( PORT >= 7788 && PORT <= 7797 )) || { echo "port 7788-7797" >&2; exit 2; }
"${SSH[@]}" 'sudo ufw status | grep -q "^7788:7797/udp" || sudo ufw allow 7788:7797/udp >/dev/null'
"${SSH[@]}" "sudo docker run -d --name sennite-match-$MATCH --label sennite.match=$MATCH --label sennite.port=$PORT \
  --network host --cap-drop ALL --security-opt no-new-privileges:true --memory 2g --restart no \
  --log-driver json-file --log-opt max-size=10m --log-opt max-file=3 \
  -v /opt/sennite/server/match-secret:/run/match-secret:ro \
  sennite-match:latest '/Game/Maps/Arena?Bases=$BASES?Match=$MATCH' -port=$PORT -unattended -stdout \
  -PublisherUrl=http://127.0.0.1:8790 -MatchSecretFile=/run/match-secret"
echo "match $MATCH: players join at $HOST:$PORT (logs: ssh … sudo docker logs -f sennite-match-$MATCH)"
