# The publisher's match server: the game's dedicated server for Linux (Tools/build_linux_match_server.sh stages
# it into LinuxServer/ beside this file). Runs as uid 10001; the engine refuses to run as root.
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/* \
 && groupadd -g 10001 sennite && useradd -u 10001 -g 10001 -m -d /home/sennite sennite
COPY --chown=10001:10001 LinuxServer /srv/match
USER 10001:10001
WORKDIR /srv/match
ENTRYPOINT ["/srv/match/Sennite/Binaries/Linux/SenniteServer", "Sennite"]
