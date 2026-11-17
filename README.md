# caffeinateLid

Like `caffeinate`, but also survives closing the lid: sets `pmset -a disablesleep 1` while running and
restores the previous value when interrupted (Ctrl-C, SIGTERM from `timeout`, SIGHUP), and even when the
main process is SIGKILLed — a signal-immune watchdog does the restore.

```sh
sudo timeout 3h caffeinateLid
```

Build: `nix build` (runs the dry-run tests), or `cc -O2 -o caffeinateLid caffeinateLid.c`.

Test without root: `./test.sh` (uses `CAFFEINATELID_DRY_RUN=<state file>` instead of `pmset`).

If the machine loses power while it runs, sleep stays disabled: `sudo pmset -a disablesleep 0`.
