#!/bin/sh
# Cloudflare setup for smz3.com with the cloudflare skill's cf.py. Dry run unless YES=1.
# Steps 3-4 only after Caddy serves the origin certificate and client_auth (server/deploy/APPLY.md).
# usage: ORIGIN_IP=<vm address> [YES=1] sh apply.sh <step>
set -eu
CF="python3 $HOME/.claude/skills/cloudflare/cf.py ${YES:+--yes}"
Z=smz3.com
D=$(dirname "$0")
case "${1:-}" in
1)  # settings that are safe before the origin changes
    $CF setting $Z always_use_https off
    $CF setting $Z automatic_https_rewrites off
    $CF setting $Z min_tls_version 1.2
    $CF setting $Z tls_1_3 on
    $CF setting $Z http3 on
    $CF setting $Z 0rtt off
    $CF setting $Z websockets on
    $CF setting $Z always_online off
    $CF setting $Z browser_check on
    $CF setting $Z security_level medium
    $CF bot $Z off
    $CF rules $Z waf "$D/waf.json"
    $CF rules $Z ratelimit "$D/ratelimit.json"
    $CF rules $Z config "$D/config.json"
    $CF rules $Z cache "$D/cache.json"
    $CF rules $Z redirect "$D/redirect.json" ;;
2)  # point the names at the VM (proxied); replaces Namecheap's parking records
    : "${ORIGIN_IP:?set ORIGIN_IP}"
    $CF setting $Z ssl full
    $CF dns $Z set @ --type A --content "$ORIGIN_IP"
    $CF dns $Z set api --type A --content "$ORIGIN_IP"
    $CF dns $Z set www --type CNAME --content smz3.com ;;
3)  $CF setting $Z ssl strict ;;
4)  $CF aop $Z on ;;
*)  echo "usage: ORIGIN_IP=<ip> [YES=1] sh $0 1|2|3|4"; exit 1 ;;
esac
