# Applying server/deploy to the VM

Apply one step at a time and check it before starting the next. Keep a second SSH session open the whole time, and
never close it while changing sshd or the firewall: it is the way back if a new login fails. `<VM>` is the VM's SSH address.

```sh
rsync -a server/deploy/ ubuntu@<VM>:deploy/      # from the repo; on the VM, D=~/deploy
sudo mkdir -p /root/pre-deploy && sudo cp -a /etc/caddy /etc/ssh/sshd_config.d /etc/iptables /root/pre-deploy/
```

## 1. sysctl

```sh
sudo install -m 0644 $D/sysctl.d/60-smz3.conf /etc/sysctl.d/ && sudo sysctl --system
```
Check: `sysctl net.ipv4.conf.all.rp_filter net.ipv4.tcp_syncookies`, then `curl -sI https://smz3-universe.duckdns.org/play/`.
Rollback: `sudo rm /etc/sysctl.d/60-smz3.conf && sudo reboot` (or set the old values by hand with `sysctl -w`).

## 2. rpcbind

As in `firewall/README.md` (last section). Check: `ss -tulpn | grep ':111 '` prints nothing.

## 3. Nightly backup and the pull user

```sh
sudo apt-get install -y zstd rsync
sudo groupadd --system smz3backup
sudo install -d -m 0750 -o root -g smz3backup /var/backups/smz3
sudo install -m 0755 $D/backup/smz3-backup.sh /usr/local/sbin/
sudo install -m 0644 $D/backup/smz3-backup.service $D/backup/smz3-backup.timer /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now smz3-backup.timer
sudo systemctl start smz3-backup.service        # the first archive now
```
Check: `systemctl status smz3-backup.service` (exited 0), `ls -l /var/backups/smz3`, `systemctl list-timers smz3-backup.timer`,
`tar --zstd -tf /var/backups/smz3/smz3-*.tar.zst | head`. Then the `smz3pull` user and the home side: `backup/PULL.md`.
Rollback: `sudo systemctl disable --now smz3-backup.timer`, remove the two units and the script.

## 4. sshd

```sh
sudo install -m 0644 $D/sshd_config.d/50-hardening.conf /etc/ssh/sshd_config.d/
sudo sshd -t && sudo systemctl reload ssh
sudo sshd -T | grep -Ei '^(permitrootlogin|passwordauthentication|kbdinteractiveauthentication|maxauthtries|allowusers|allowtcpforwarding|allowagentforwarding|x11forwarding)'
```
`sshd -T` must show the values of the file: if `50-cloud-init.conf` sets one of them differently it wins (first value
read), so fix it there. Check: a NEW login as ubuntu from a third terminal, and `ssh -L` to a local port still works.
Rollback (from the open session): `sudo rm /etc/ssh/sshd_config.d/50-hardening.conf && sudo systemctl reload ssh`.

## 5. Unattended upgrades and needrestart

```sh
sudo install -m 0644 $D/apt/52unattended-upgrades-smz3 /etc/apt/apt.conf.d/
sudo install -D -m 0644 $D/needrestart/conf.d/50-auto.conf /etc/needrestart/conf.d/50-auto.conf
```
Check: `apt-config dump | grep -i 'Automatic-Reboot\|Remove-Unused-Kernel'`, `sudo unattended-upgrade --dry-run --debug 2>&1 | tail`.
Rollback: remove both files.

## 6. smz3.service sandbox

First `getent passwd smz3`: the home field must be under `/srv/smz3` (or anything outside `/home`, `/root`, `/run/user`),
or ProtectHome hides it. Nothing in the app reads its home.

```sh
sudo install -D -m 0644 $D/smz3.service.d/hardening.conf /etc/systemd/system/smz3.service.d/hardening.conf
sudo systemctl daemon-reload && sudo systemctl restart smz3
```
Check: `systemctl status smz3`, `journalctl -u smz3 -n 50` (no EACCES/EPERM/EROFS), `curl -s 127.0.0.1:3360/api/version`
(shows a tag: outbound HTTPS and DNS work), `curl -s 127.0.0.1:3360/api/daily | head -c 200`, and a daily start, a race
draft and a Map Rando seed from a client (the child binaries run inside the same sandbox).
`systemd-analyze security smz3` shows the score.
Rollback: `sudo rm /etc/systemd/system/smz3.service.d/hardening.conf && sudo systemctl daemon-reload && sudo systemctl restart smz3`.

## 7. caddy.service sandbox

```sh
sudo install -D -m 0644 $D/caddy.service.d/hardening.conf /etc/systemd/system/caddy.service.d/hardening.conf
sudo systemctl daemon-reload && sudo systemctl restart caddy
```
Check: `systemctl status caddy`, `journalctl -u caddy -n 50`, `curl -sI https://smz3-universe.duckdns.org/play/`,
`curl -s http://smz3-universe.duckdns.org/api/version`.
Rollback: remove the drop-in, `daemon-reload`, `restart caddy`.

## 8. Caddyfile

Origin files: the Cloudflare origin certificate for `smz3.com, *.smz3.com`, and the CA of our own Authenticated
Origin Pulls client certificate (made in step 9; until then use `mode verify_if_given` with any CA file):

```sh
sudo install -d -m 0750 -o root -g caddy /etc/caddy/cf
sudo install -m 0644 origin.pem /etc/caddy/cf/origin.pem
sudo install -m 0640 -g caddy origin.key /etc/caddy/cf/origin.key
sudo install -m 0644 aop-ca.pem /etc/caddy/cf/origin-pull-ca.pem
sudo install -d -m 0755 /srv/smz3-site       # the landing page: frontends/site/deploy.sh puts its files here
sudo install -m 0644 $D/Caddyfile $D/cloudflare-ips.caddy /etc/caddy/
```

With `mode require_and_verify`, HTTPS on the Cloudflare names fails until step 9 is done, so a first install
starts with `mode verify_if_given`.

```sh
sudo caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile && sudo systemctl reload caddy
```
A failed reload keeps the running config. Check duckdns first (unchanged for current clients):
`curl -sI https://smz3-universe.duckdns.org/play/` (COOP/COEP, no `Server`), `curl -s http://smz3-universe.duckdns.org/api/version`.

Then Cloudflare (`cloudflare/apply.sh` steps 1-2), and through it:
```sh
curl -sI https://smz3.com/                       # landing, HSTS, CSP
curl -sI https://www.smz3.com/x                  # 301 to https://smz3.com/x
curl -sI https://smz3.com/play/                  # COOP/COEP
curl -s  https://smz3.com/ladder | head -c 100   # the ladder page
curl -s  https://smz3.com/api/version
curl -sI https://smz3.com/download               # 302 to GitHub
curl -s -o /dev/null -w '%{http_code}\n' https://smz3.com/api/admin     # 404
curl -s  http://api.smz3.com/api/version ; curl -s https://api.smz3.com/api/version
curl -sk --resolve smz3.com:443:<VM> https://smz3.com/ ; echo $?          # from outside Cloudflare: no answer
tail -n 3 /var/log/caddy/access.log              # client_ip is the visitor, remote_ip a Cloudflare address
```
Then Cloudflare steps 3-4 (SSL strict, Authenticated Origin Pulls on) and step 9.
Rollback: `sudo cp -a /root/pre-deploy/caddy/. /etc/caddy/ && sudo systemctl reload caddy`.

Refreshing the Cloudflare ranges (then validate and reload; same list in `fail2ban/jail.local` and the firewall):
```sh
{ printf '# Cloudflare ranges (www.cloudflare.com/ips-v4, ips-v6); refresh with the command in APPLY.md.\n# `import cf_ips <words>` expands to one line: <words> followed by every range.\n(cf_ips) {\n\t{args[:]}'
  { curl -fsS https://www.cloudflare.com/ips-v4; echo; curl -fsS https://www.cloudflare.com/ips-v6; echo; } | grep . | sed 's/^/ /' | tr -d '\n'
  printf '\n}\n'; } | sudo tee /etc/caddy/cloudflare-ips.caddy >/dev/null
```

## 9. Authenticated Origin Pulls with our own certificate

With the global setting alone Cloudflare presents its shared certificate, the same for every Cloudflare zone, so
anyone could point a zone of their own at the VM and get through; in practice it sent no certificate at all (520
with `require_and_verify`). So the zone uploads a client certificate signed by a CA of our own, and Caddy trusts
only that CA. Make both on the admin machine, never in the repo (EC P-256, 10 years):

```sh
umask 077; mkdir aop && cd aop
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 3650 \
  -keyout ca.key -out ca.pem -subj "/CN=smz3.com origin pull CA"
openssl req -new -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
  -keyout client.key -out client.csr -subj "/CN=cloudflare origin pull smz3.com"
printf 'basicConstraints=CA:FALSE\nkeyUsage=digitalSignature\nextendedKeyUsage=clientAuth\n' > ext.cnf
openssl x509 -req -in client.csr -CA ca.pem -CAkey ca.key -CAcreateserial -days 3650 -extfile ext.cnf -out client.pem
python3 - <<'PY' > upload.json
import json; print(json.dumps({"certificate": open("client.pem").read(), "private_key": open("client.key").read()}))
PY
python3 $CF --yes raw POST /zones/<zone id>/origin_tls_client_auth upload.json && rm upload.json
python3 $CF raw GET /zones/<zone id>/origin_tls_client_auth        # status: active
```

Then `aop-ca.pem` (= `ca.pem`) to `/etc/caddy/cf/origin-pull-ca.pem`, `mode require_and_verify`, validate,
`chown caddy:caddy /var/log/caddy/access.log` (a validate run as root can leave it root-owned and the reload fails),
reload, and repeat the smz3.com and api.smz3.com checks. The access log shows
`"client_common_name":"cloudflare origin pull smz3.com"`. Before the client certificate expires, sign a new one with the
same CA and upload it; delete the old one with `DELETE /zones/<zone id>/origin_tls_client_auth/<id>`.
Rollback: `mode verify_if_given`, validate, reload.

## 10. fail2ban

After step 8, so `/var/log/caddy/access.log` exists.
```sh
sudo apt-get install -y fail2ban python3-systemd
sudo install -m 0644 $D/fail2ban/jail.local /etc/fail2ban/jail.local
sudo install -m 0644 $D/fail2ban/filter.d/caddy-4xx.conf /etc/fail2ban/filter.d/
sudo fail2ban-client -t && sudo systemctl restart fail2ban
```
Check: `sudo fail2ban-client status sshd`, `sudo fail2ban-client status caddy-4xx`,
`sudo fail2ban-regex /var/log/caddy/access.log /etc/fail2ban/filter.d/caddy-4xx.conf` (matches only duckdns 4xx lines).
Unban: `sudo fail2ban-client set <jail> unbanip <ip>`. Rollback: `sudo systemctl disable --now fail2ban`.

## 11. Firewall phase B

Not now: only once no client uses duckdns. `firewall/README.md`.
