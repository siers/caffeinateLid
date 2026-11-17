# caffeinateLid

Keep a Mac awake **with the lid closed** for as long as the command runs. When it stops, the previous sleep
setting comes back.

macOS's `caffeinate` can't stop the Mac from sleeping when you close the lid. `pmset -a disablesleep 1` can,
but it is a system-wide setting saved to disk: it stays on until something turns it off, even across reboots.
caffeinateLid turns it on only while it runs, the way `caffeinate` works, and makes sure it is turned back
off however the process ends.

## Usage

```sh
sudo caffeinateLid                         # until Ctrl-C
sudo timeout 3h caffeinateLid              # for three hours (GNU coreutils timeout)
sudo "$(which timeout)" 3h caffeinateLid   # if sudo's PATH doesn't find timeout (e.g. Nix, Homebrew)
```

Output:

```text
caffeinateLid: sleep disabled (lid close won't sleep); interrupt to restore
^C
caffeinateLid: restored disablesleep 0
```

Check the current state with `pmset -g | grep SleepDisabled`.

### Install

```sh
nix profile install github:siers/caffeinateLid    # or: nix run github:siers/caffeinateLid
# without Nix:
cc -O2 -o caffeinateLid caffeinateLid.c && sudo install -m 755 caffeinateLid /usr/local/bin/
```

## Mechanics

1. **Signals blocked first.** The first thing the program does is block SIGINT, SIGTERM, SIGHUP and SIGQUIT.
   An interrupt that arrives during startup waits until startup is done, so it can't skip the restore.
   It also installs real handlers for these signals, replacing any "ignore" setting inherited from `nohup`
   or `&`. Without that, the stop signal would be thrown away and the program would never return.
2. **Save the current value.** It reads `SleepDisabled` from `pmset -g` and restores that value at the end.
   If sleep was already disabled before it started, it stays disabled.
3. **Watchdog.** It starts a helper process (the watchdog) connected to the main process by a pipe. The
   watchdog runs in its own session (`setsid`) and ignores every signal it can, so neither Ctrl-C on the
   terminal nor a signal sent to the whole process group reaches it. It just waits on the pipe.
4. **Disable sleep**, then wait for a stop signal with `sigwait`. Repeated Ctrl-Cs are absorbed.
5. **Restore.** The main process closes its end of the pipe. The watchdog sees end-of-file, runs
   `pmset -a disablesleep <previous>` (retrying up to 5 times), and exits. The main process waits for it and
   prints `restored` only if the restore succeeded.

The pipe closes whenever the main process exits, including when it is SIGKILLed (for example by
`timeout -k`). So the restore happens even when the main process can't run any cleanup code.

Exit codes: `0` on a normal stop, `1` if setting or restoring failed, `2` for bad arguments.

### Limits

- **Power loss, or a SIGKILL sent to the watchdog itself:** sleep stays disabled. Fix it with
  `sudo pmset -a disablesleep 0`.
- **Overlapping runs:** if two copies run at the same time, the first one to stop restores `0` while the
  second is still running.
- **Some hardware ignores the setting:** on some Apple Silicon models running recent macOS, lid-close sleep
  reportedly still happens without an external display. Close the lid once to check yours.

## Testing

```sh
./test.sh        # or: nix build (runs it in the check phase)
```

`CAFFEINATELID_DRY_RUN=<file>` turns on a dry run: no root needed and no `pmset`. The simulated
`SleepDisabled` value is kept in `<file>`, read and written by `/bin/sh` commands that are started and
waited for exactly the way `pmset` is.

The tests check the value while running and after stopping, for:

- `timeout`
- SIGINT, SIGHUP and SIGKILL
- `timeout -k`
- repeated SIGINT to the whole process group
- an already-disabled starting state
- bad arguments
- a clean exit with no false "FAILED" message
