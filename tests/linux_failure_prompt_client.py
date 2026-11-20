#!/usr/bin/env python3
"""Run against isoduo_linux_failure_prompt in a native desktop session."""
import os
import re
import shlex
from pathlib import Path
import subprocess
import sys
import tempfile

from linux_accessibility_client import Atspi, GLib, children, invoke, until


def descendants(node):
    for child in children(node):
        yield child
        yield from descendants(child)


def select(title, label):
    def button():
        for app in children(Atspi.get_desktop(0)):
            try:
                for window in children(app):
                    if window.get_name() == title:
                        match = next((node for node in descendants(window)
                                      if node.get_name() == label and node.get_role() == Atspi.Role.PUSH_BUTTON), None)
                        if match is not None:
                            return match
            except GLib.Error:
                # The previous dialog's process may exit while AT-SPI
                # is removing it from the desktop's application list.
                continue
        return None

    assert invoke(until(button)), (title, label)


def main_window(report):
    """The fixture's native main window: ("x11", XID) or ("wayland", 0)."""
    match = until(lambda: re.search(r"^Main window (x11|wayland) (\d+)$", report(), re.M))
    return match[1], int(match[2])


def helper_arguments():
    """Argument lists of the running GTK recovery helpers."""
    found = []
    for pid in subprocess.run(["pgrep", "-f", "isoduo_settings_recovery_dialog"],
                              capture_output=True, text=True).stdout.split():
        try:
            arguments = Path(f"/proc/{pid}/cmdline").read_bytes().decode().split("\0")[:-1]
        except OSError:
            continue
        if arguments and arguments[0].endswith("isoduo_settings_recovery_dialog"):
            found.append(arguments)
    return found


def transient_parent(title):
    """WM_TRANSIENT_FOR of the X11 top-level window named title, or None."""
    clients = subprocess.run(["xprop", "-root", "_NET_CLIENT_LIST"], capture_output=True, text=True, check=True).stdout
    for window in re.findall(r"0x[0-9a-f]+", clients):
        props = subprocess.run(["xprop", "-id", window, "_NET_WM_NAME", "WM_TRANSIENT_FOR"],
                               capture_output=True, text=True).stdout
        if f'"{title}"' in props:
            parent = re.search(r"WM_TRANSIENT_FOR\(WINDOW\): window id # (0x[0-9a-f]+)", props)
            return int(parent[1], 16) if parent else None
    return None


def check_attachment(case, directory, backend, xid):
    """On X11 every Zenity prompt is attached to the main window; on Wayland none is (a known platform limit)."""
    logged = Path(directory) / "zenity-args"
    calls = logged.read_text(encoding="utf-8").splitlines() if logged.exists() else []
    for call in calls:
        if backend == "x11":
            assert f"--attach={xid}" in call.split(" "), (case, call)
        else:
            assert "--attach" not in call, (case, call)
    return len(calls)


def check_helper_attachment(case, backend, xid):
    """The GTK helper gets the main window's XID on X11 and is transient for it; on Wayland it gets none."""
    for arguments in until(helper_arguments):
        if backend == "x11":
            assert len(arguments) == 5 and arguments[4] == str(xid), (case, arguments)
        else:
            assert len(arguments) == 4, (case, arguments)
    if backend == "x11":
        assert until(lambda: transient_parent("畫面無法繪製")) == xid, case
    print("ATTACHED helper", case, backend, xid if backend == "x11" else "unparented", flush=True)


def exercise(executable, case):
    with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryFile() as output:
        zenity = Path(directory) / "zenity"
        def quit_then(command):
            return f"""
                if test -e asked; then {command}; fi
                /usr/bin/touch asked
                printf '%s\\n' {shlex.quote("結束")}
                exit "${{ZENITY_ESC:-1}}"
            """
        script = {
            "missing": None,
            "system-zenity": None,
            "gtk-startup": quit_then("exit 1"),
            "exit": "echo '捨棄'; exit 2",
            "signalled": "kill -TERM $$",
            "startup-one": "exit 1",
            "empty-success": quit_then("exit 0"),
            "invalid-discard": quit_then("echo '捨棄'; exit 2"),
            "cancel": f"""
                if test -e cancelled; then exit 2; fi
                case "$*" in
                    *--extra-button={shlex.quote("取消")}*)
                        /usr/bin/touch cancelled; printf '%s\\n' {shlex.quote("取消")};;
                    *) printf '%s\\n' {shlex.quote("結束")};;
                esac
                exit "$ZENITY_ESC"
            """,
        }[case]
        if script is not None:
            # Record each command line so the test can check how the prompt was attached.
            record = 'printf \'%s\\n\' "$*" >> zenity-args\n'
            zenity.write_text("#!/bin/sh\n" + record + script + "\n", encoding="utf-8")
            zenity.chmod(0o700)
        env = dict(os.environ, GTK_A11Y="atspi")
        env.pop("NO_AT_BRIDGE", None)
        if case != "system-zenity":
            env["PATH"] = directory
        if case == "gtk-startup":
            env["GDK_BACKEND"] = "invalid"
        host = subprocess.Popen([executable, "save-error" if case == "exit" else case],
                                env=env, cwd=directory, stdout=output, stderr=subprocess.STDOUT)
        def report():
            return os.pread(output.fileno(), 65536, 0).decode()
        try:
            backend, xid = main_window(report)
            if case == "gtk-startup":
                until(lambda: "Cannot initialize GTK for renderer recovery" in report()
                      and "with status 2" in report())
                assert host.poll() is None and "Save " not in report(), report()
            else:
                if case == "exit":
                    check_helper_attachment(case, backend, xid)
                if case == "system-zenity" and backend == "x11":
                    assert until(lambda: transient_parent("畫面無法繪製")) == xid, case
                    print("ATTACHED system zenity", xid, flush=True)
                if case in ("exit", "signalled", "system-zenity"):
                    select("畫面無法繪製", "結束")
                    select("要儲存修改嗎？", "捨棄" if case == "signalled" else "儲存")
                if case == "exit":
                    select("無法儲存", "確定")
                if case in ("empty-success", "invalid-discard"):
                    select("要儲存修改嗎？", "取消")
                if case in ("signalled", "system-zenity"):
                    expected = f"Closed frozen with {int(case == 'system-zenity')} saves"
                else:
                    select("畫面無法繪製", "重試")
                    expected = f"Rebuilt with {int(case == 'exit')} saves"
                assert host.wait(timeout=8) == 0
                assert expected in report(), report()
                if case == "system-zenity":
                    assert "Renderer recovery dialog failed" not in report(), report()
            if case == "cancel":
                assert (Path(directory) / "cancelled").exists()
            calls = check_attachment(case, directory, backend, xid)
            print("ATTACHED zenity", case, backend, calls, "calls", flush=True)
        finally:
            if host.poll() is None:
                host.terminate()
                host.wait(timeout=8)
            print(report(), end="")
        print("PASS", case)


if __name__ == "__main__":
    Atspi.init()
    for case in sys.argv[2:] or ("missing", "exit", "signalled", "cancel", "startup-one",
                               "empty-success", "invalid-discard", "gtk-startup"):
        exercise(str(Path(sys.argv[1]).resolve()), case)
