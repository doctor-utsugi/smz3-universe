# Off-site backup: pulled from home

The VM keeps the last 14 nightly archives in `/var/backups/smz3` (root:smz3backup, 0640). A machine at home pulls them over
SSH with rsync, so the VM never holds a home address or a key that opens anything at home.

The pull user is `smz3pull`: Ubuntu already has a system user called `backup`.

## On the VM (once)

```sh
sudo groupadd --system smz3backup        # if APPLY.md has not made it yet
sudo useradd --system --create-home --home-dir /var/lib/smz3pull --shell /bin/sh --groups smz3backup smz3pull
sudo install -d -m 0700 -o smz3pull -g smz3pull /var/lib/smz3pull/.ssh
# One line in /var/lib/smz3pull/.ssh/authorized_keys (0600, owned by smz3pull), with the home machine's public key:
#   command="rrsync -ro /var/backups/smz3",restrict ssh-ed25519 AAAA... smz3-pull
sudo -u smz3pull ls /var/backups/smz3    # the group lets it read the archives
```

`restrict` turns off forwarding, the agent, X11 and the TTY; `rrsync -ro` (in the rsync package, `/usr/bin/rrsync`) only
lets rsync read inside `/var/backups/smz3`, whatever command the client asks for. `smz3pull` is in `AllowUsers`
(`sshd_config.d/50-hardening.conf`); its shell must be a real shell for the forced command to run, and it has no password.

## On the home machine

Make a key just for this, with no passphrase so a timer can use it:

```sh
ssh-keygen -t ed25519 -N '' -C smz3-pull -f ~/.ssh/smz3_pull
```

Pull (paths are relative to `/var/backups/smz3` because of rrsync):

```sh
rsync -a --delete -e "ssh -i ~/.ssh/smz3_pull -o IdentitiesOnly=yes" smz3pull@<VM>:/ <LOCAL_DIR>/
```

`--delete` mirrors the VM's 14-day window; drop it to keep every archive at home. Run it daily after 04:00 UTC (cron or
a systemd user timer). Check the first run with `ssh -i ~/.ssh/smz3_pull smz3pull@<VM>`: it must refuse a shell.

The archives hold the server's HMAC `secret` and every player record: keep `<LOCAL_DIR>` private.

## Restore

```sh
sudo systemctl stop smz3 redis-server
sudo mkdir -p /tmp/restore
sudo tar --zstd -xf smz3-YYYYMMDD.tar.zst -C /tmp/restore
sudo rsync -a /tmp/restore/data/ /srv/smz3/app/data/ && sudo chown -R smz3:smz3 /srv/smz3/app/data
sudo install -o redis -g redis -m 0660 /tmp/restore/dump.rdb /var/lib/redis/dump.rdb
sudo systemctl start redis-server smz3
```

If Redis runs with `appendonly yes`, it loads the AOF instead of `dump.rdb`: turn it off for the restore, or rewrite it after.
