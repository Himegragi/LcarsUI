#!/usr/bin/env python3
"""Run against isoduo_linux_failure_prompt in a native desktop session."""
import os
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


def exercise(executable, case):
    with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryFile() as output:
        zenity = Path(directory) / "zenity"
        script = {
            "missing": None,
            "exit": "echo '捨棄'; exit 2",
            "signalled": "kill -TERM $$",
            "cancel": """
                if test -e cancelled; then exit 2; fi
                case "$*" in
                    *--default-cancel*) touch cancelled; exit 1;;
                    *) exit 1;;
                esac
            """,
        }[case]
        if script is not None:
            zenity.write_text("#!/bin/sh\n" + script.replace("touch cancelled", "/usr/bin/touch cancelled") + "\n", encoding="utf-8")
            zenity.chmod(0o700)
        env = dict(os.environ, PATH=directory)
        host = subprocess.Popen([executable, "save-error" if case == "exit" else case],
                                env=env, cwd=directory, stdout=output, stderr=subprocess.STDOUT)
        try:
            if case == "exit":
                select("畫面無法繪製", "結束")
                select("要儲存修改嗎？", "儲存")
                select("無法儲存", "確定")
            if case == "signalled":
                select("畫面無法繪製", "結束")
                select("要儲存修改嗎？", "捨棄")
                expected = "Closed frozen with 0 saves"
            else:
                select("畫面無法繪製", "重試")
                expected = f"Rebuilt with {int(case == 'exit')} saves"
            assert host.wait(timeout=8) == 0
            report = os.pread(output.fileno(), 65536, 0).decode()
            assert expected in report, report
            if case == "cancel":
                assert (Path(directory) / "cancelled").exists()
        finally:
            if host.poll() is None:
                host.terminate()
                host.wait(timeout=8)
            print(os.pread(output.fileno(), 65536, 0).decode(), end="")
        print("PASS", case)


if __name__ == "__main__":
    Atspi.init()
    for case in sys.argv[2:] or ("missing", "exit", "signalled", "cancel"):
        exercise(str(Path(sys.argv[1]).resolve()), case)
