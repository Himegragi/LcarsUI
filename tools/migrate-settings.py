"""Copy a legacy Linux settings demo directory into the current XDG layout."""

import argparse
import ctypes
import os
from pathlib import Path
import pwd
import shutil
import stat
import tempfile


def config_root():
    config = Path(os.environ.get("XDG_CONFIG_HOME", ""))
    if config.is_absolute():
        return config
    home = Path(os.environ.get("HOME", ""))
    if not home.is_absolute():
        home = Path(pwd.getpwuid(os.getuid()).pw_dir)
    return home / ".config"


def entry_mode(path):
    mode = path.lstat().st_mode
    if stat.S_ISLNK(mode):
        raise ValueError(f"Linked paths cannot be migrated: {path}")
    return mode


def check_tree(path):
    if stat.S_ISDIR(entry_mode(path)):
        for child in path.iterdir():
            check_tree(child)
    elif not stat.S_ISREG(entry_mode(path)):
        raise ValueError(f"Only regular files and directories can be migrated: {path}")


def publish(stage, destination):
    # A concurrent settings save or migration may create the destination.
    # Ordinary rename could replace an empty directory. RENAME_NOREPLACE cannot.
    rename = ctypes.CDLL(None, use_errno=True).renameat2
    rename.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
    rename.restype = ctypes.c_int
    at_fdcwd, rename_noreplace = -100, 1
    if rename(at_fdcwd, os.fsencode(stage), at_fdcwd, os.fsencode(destination), rename_noreplace):
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), destination)


def migrate(root):
    source = root / "LcarsUI/SettingsDemo"
    destination = root / "Isoduo/SettingsDemo"
    try:
        for path in (root, source.parent, source):
            if not stat.S_ISDIR(entry_mode(path)):
                raise NotADirectoryError(path)
    except (FileNotFoundError, NotADirectoryError):
        raise ValueError(f"No previous settings directory found: {source}") from None
    if destination.parent.is_symlink():
        raise ValueError(f"Linked paths cannot be migrated: {destination.parent}")
    if os.path.lexists(destination):
        raise ValueError(f"Destination already exists; nothing changed: {destination}")
    check_tree(source)
    destination.parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="migration-", dir=destination.parent) as temporary:
        stage = Path(temporary) / "settings"
        shutil.copytree(source, stage, symlinks=True)
        check_tree(stage)
        publish(stage, destination)
    print(f"Copied settings to {destination}. Backup retained at {source}.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config-root", type=Path, help="override the XDG configuration directory")
    args = parser.parse_args()
    try:
        migrate(args.config_root.absolute() if args.config_root is not None else config_root())
    except (OSError, ValueError, KeyError, AttributeError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
