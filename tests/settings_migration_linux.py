import os
from pathlib import Path
import subprocess
import sys
import tempfile

migration = Path(__file__).resolve().parents[1] / "tools" / "migrate-settings.py"


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def run(root, *, explicit=False):
    environment = dict(os.environ, XDG_CONFIG_HOME=str(root))
    arguments = [sys.executable, str(migration)]
    if explicit:
        environment["XDG_CONFIG_HOME"] = str(root / "unused")
        arguments += ["--config-root", str(root)]
    return subprocess.run(arguments, env=environment, text=True, capture_output=True, timeout=10)


with tempfile.TemporaryDirectory(prefix="isoduo-migration-") as directory:
    root = Path(directory) / "舊 設定"
    source = root / "LcarsUI/SettingsDemo"
    destination = root / "Isoduo/SettingsDemo"
    fixture = {
        "settings.json": bytes([0, 1, 2, 255]),
        "backup/settings.json": b"previous backup",
        "backup/.hidden.json": b"hidden backup",
    }
    for name, data in fixture.items():
        write(source / name, data)
    result = run(root, explicit=True)
    assert result.returncode == 0, result.stderr
    for name, data in fixture.items():
        assert (source / name).read_bytes() == (destination / name).read_bytes() == data
    print("PASS active file and nested and hidden backups")

    write(destination / "settings.json", b"current")
    result = run(root)
    assert result.returncode != 0 and "already exists" in result.stderr
    assert (destination / "settings.json").read_bytes() == b"current"
    assert (source / "settings.json").read_bytes() == fixture["settings.json"]
    result = run(root / "missing")
    assert result.returncode != 0 and "No previous settings directory" in result.stderr
    assert not (root / "missing").exists()
    print("PASS existing destination and missing source")

    for index, backup in enumerate(("backup/settings.json", "settings.json.bad")):
        backup_root = root / f"backup-only-{index}"
        old = backup_root / "LcarsUI/SettingsDemo" / backup
        new = backup_root / "Isoduo/SettingsDemo"
        write(old, fixture["settings.json"])
        result = run(backup_root)
        assert result.returncode == 0, result.stderr
        assert (new / backup).read_bytes() == old.read_bytes() == fixture["settings.json"]
        assert not (new / "settings.json").exists()
    print("PASS backup-only documents remain backups")

    link_root = root / "links"
    write(link_root / "LcarsUI/SettingsDemo/settings.json", b"original")
    (link_root / "LcarsUI/SettingsDemo/.linked").symlink_to(destination, target_is_directory=True)
    result = run(link_root)
    assert result.returncode != 0 and "Linked paths" in result.stderr
    assert not (link_root / "Isoduo/SettingsDemo").exists()
    assert not list(link_root.glob("Isoduo/migration-*"))
    print("PASS hidden symlink rejected with no published copy or staging")
