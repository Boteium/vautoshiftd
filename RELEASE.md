# Pre-built releases

Building from source? Use `sudo make install` as described in [README.md](README.md) — do not use the scripts in a release tarball.

## Download

Download the latest Linux tarball from [GitHub Releases](https://github.com/Boteium/vautoshiftd/releases).

Each release ships one archive per architecture, e.g. `vautoshiftd-v1.0.0-linux-amd64.tar.gz`.

## Tarball contents

```text
vautoshiftd-v1.0.0-linux-amd64/
├── vautoshiftd
├── vautoshiftd.service
├── etc/default/vautoshiftd
├── install.sh
├── uninstall.sh
└── SHA256SUMS
```

The binary is statically linked for Linux x86_64 and does not require a separate runtime.

## Install

Extract the archive and run the install script from inside the extracted folder:

```bash
tar xzf vautoshiftd-v1.0.0-linux-amd64.tar.gz
cd vautoshiftd-v1.0.0-linux-amd64
sha256sum -c SHA256SUMS   # optional
sudo ./install.sh
sudo systemctl enable --now vautoshiftd.service
```

To change the default 175ms autoshift timeout, edit `/etc/default/vautoshiftd`:

```bash
VAUTOSHIFTD_ARGS='--autoshift-timeout=175'
```

See [README.md](README.md) for allow/deny lists and other CLI options.

## Uninstall

From the extracted release folder:

```bash
sudo ./uninstall.sh
```

This stops and disables the service, removes the binary and systemd unit, and leaves `/etc/default/vautoshiftd` in place.

## Build a release tarball locally

From a source checkout:

```bash
make dist
```

The archive is written to `dist/`.
