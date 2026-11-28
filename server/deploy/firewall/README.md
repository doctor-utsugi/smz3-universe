# Host firewall: phase A and phase B

**Phase A (now):** Oracle's default iptables (`/etc/iptables/rules.v4`), INPUT accepts 22, 80 and 443 from anywhere, then
REJECT. The Oracle security list matches. It stays like this while `smz3-universe.duckdns.org` is served directly.

**Phase B (later, not applied):** 80 and 443 only from Cloudflare, 22 unchanged. Only after every client uses
`smz3.com` / `api.smz3.com`: phase B cuts the duckdns site off, including its Let's Encrypt renewals. Remove the duckdns
block from the Caddyfile (and the `caddy-4xx` fail2ban jail, which then sees nothing) in the same change.

## Phase B: iptables

In `/etc/iptables/rules.v4`, in `*filter`: declare the chain next to the other chains, replace the two ACCEPT lines for
`--dport 80` and `--dport 443` with the jump, and add the chain's rules before `COMMIT`. Anything the chain does not
accept falls through to the existing final REJECT.

```
:CLOUDFLARE - [0:0]
-A INPUT -p tcp -m state --state NEW -m multiport --dports 80,443 -j CLOUDFLARE
-A CLOUDFLARE -s 173.245.48.0/20 -j ACCEPT
-A CLOUDFLARE -s 103.21.244.0/22 -j ACCEPT
-A CLOUDFLARE -s 103.22.200.0/22 -j ACCEPT
-A CLOUDFLARE -s 103.31.4.0/22 -j ACCEPT
-A CLOUDFLARE -s 141.101.64.0/18 -j ACCEPT
-A CLOUDFLARE -s 108.162.192.0/18 -j ACCEPT
-A CLOUDFLARE -s 190.93.240.0/20 -j ACCEPT
-A CLOUDFLARE -s 188.114.96.0/20 -j ACCEPT
-A CLOUDFLARE -s 197.234.240.0/22 -j ACCEPT
-A CLOUDFLARE -s 198.41.128.0/17 -j ACCEPT
-A CLOUDFLARE -s 162.158.0.0/15 -j ACCEPT
-A CLOUDFLARE -s 104.16.0.0/13 -j ACCEPT
-A CLOUDFLARE -s 104.24.0.0/14 -j ACCEPT
-A CLOUDFLARE -s 172.64.0.0/13 -j ACCEPT
-A CLOUDFLARE -s 131.0.72.0/22 -j ACCEPT
```

`/etc/iptables/rules.v6` (only if the VM has IPv6): the same jump and chain, and keep (or add) a final
`-A INPUT -j REJECT --reject-with icmp6-adm-prohibited` after the ESTABLISHED, `ipv6-icmp`, `lo` and port 22 rules.

```
:CLOUDFLARE - [0:0]
-A INPUT -p tcp -m state --state NEW -m multiport --dports 80,443 -j CLOUDFLARE
-A CLOUDFLARE -s 2400:cb00::/32 -j ACCEPT
-A CLOUDFLARE -s 2606:4700::/32 -j ACCEPT
-A CLOUDFLARE -s 2803:f800::/32 -j ACCEPT
-A CLOUDFLARE -s 2405:b500::/32 -j ACCEPT
-A CLOUDFLARE -s 2405:8100::/32 -j ACCEPT
-A CLOUDFLARE -s 2a06:98c0::/29 -j ACCEPT
-A CLOUDFLARE -s 2c0f:f248::/32 -j ACCEPT
```

Apply and roll back (keep a second SSH session open; port 22 is untouched, but a syntax error drops the whole file):

```sh
sudo cp -a /etc/iptables/rules.v4 /root/rules.v4.phaseA            # same for rules.v6
sudo iptables-restore --test < /etc/iptables/rules.v4             # syntax check, changes nothing
sudo iptables-restore < /etc/iptables/rules.v4                    # live; netfilter-persistent loads it at boot
# rollback:
sudo iptables-restore < /root/rules.v4.phaseA && sudo cp /root/rules.v4.phaseA /etc/iptables/rules.v4
```

Check: `curl -sI https://smz3.com/` and `curl -sI http://api.smz3.com/api/version` work; a direct connection to the VM's
address on 443 from outside Cloudflare is refused; `sudo iptables -L CLOUDFLARE -nv` counts packets. The Cloudflare
ranges change rarely; refresh them here and in `cloudflare-ips.caddy` together (APPLY.md).

## Phase B: Oracle security list

The VCN security list (or an NSG on the VNIC) should say the same, so the iptables rules are a second layer:

- Keep: TCP 22 from `0.0.0.0/0`, ICMP type 3 code 4 from `0.0.0.0/0`, ICMP type 3 from the VCN's CIDR.
- Remove: TCP 80 and TCP 443 from `0.0.0.0/0` (and `::/0`).
- Add, stateful, for each Cloudflare range above: TCP destination port 80 and TCP destination port 443 (two rules per
  range, 44 in all; the default limit is 200 per list). IPv6 ranges only if the subnet has IPv6.

With the OCI CLI, `oci network security-list update --security-list-id <OCID> --ingress-security-rules file://ingress.json`
replaces the whole ingress list, so `ingress.json` must also carry the rules kept above. One way to build the added part:

```sh
{ curl -fsS https://www.cloudflare.com/ips-v4; echo; curl -fsS https://www.cloudflare.com/ips-v6; echo; } | grep . |
  jq -Rn '[inputs as $c | (80, 443) as $p | {protocol: "6", source: $c, isStateless: false,
    tcpOptions: {destinationPortRange: {min: $p, max: $p}}}]' > cloudflare-ingress.json
```

Rollback: put back the `0.0.0.0/0` rules for 80 and 443 (console: Networking > VCN > Security Lists > Ingress Rules).

## rpcbind (do in phase A)

Nothing here uses NFS or RPC; rpcbind only listens on 111 (closed by the firewall, but no reason to run it).

```sh
findmnt -t nfs,nfs4                      # must print nothing
apt-cache rdepends --installed rpcbind   # what installed package needs it (often nfs-common)
sudo apt-get purge --simulate rpcbind    # shows everything a purge would remove
sudo systemctl disable --now rpcbind.service rpcbind.socket
sudo apt-get purge rpcbind               # only if the simulation removes nothing you use; else: sudo systemctl mask rpcbind.service rpcbind.socket
ss -tulpn | grep ':111 '                 # must print nothing
```

Rollback: `sudo apt-get install rpcbind` (or `systemctl unmask` + `enable --now`).
