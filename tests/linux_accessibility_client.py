#!/usr/bin/env python3
"""Run with the fixture binary in a native session with PyGObject and AT-SPI.

Use --text-boundaries for targeted range checks or run --text-boundaries-headless
under dbus-run-session to exercise the same bridge without a display.
Use --renderer-failure to check action discovery after a native device reset.
Use --queued-events to check text and menu changes before the host dispatches them.
"""
from contextlib import contextmanager
import os
import subprocess
import sys
import tempfile
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib


def pump():
    context = GLib.MainContext.default()
    while context.pending():
        context.iteration(False)


def until(predicate):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        pump()
        if result := predicate():
            return result
        time.sleep(0.03)
    raise AssertionError("Timed out waiting for " + predicate.__name__)


def children(node):
    return [child for i in range(node.get_child_count())
            if (child := node.get_child_at_index(i)) is not None]


def windows():
    for app in children(Atspi.get_desktop(0)):
        if app.get_name() == "Isoduo":
            yield from children(app)


def window(mode):
    return next((node for node in windows()
                 if node.get_name() == "Isoduo Accessibility " + mode), None)


def find(parent, identifier):
    return next((node for node in children(parent)
                 if node.get_accessible_id() == "fixture." + identifier), None)


def has(node, state):
    return node.get_state_set().contains(getattr(Atspi.StateType, state))


def invoke(node, index=0):
    return node.get_action_iface().do_action(index)


@contextmanager
def fixture(executable, mode):
    with tempfile.TemporaryFile() as output:
        host = subprocess.Popen([executable, mode], stdout=output, stderr=subprocess.STDOUT)
        try:
            yield host, lambda: os.pread(output.fileno(), 1024 * 1024, 0).decode(errors="replace")
        finally:
            if host.poll() is None:
                host.terminate()
                host.wait(timeout=8)
            print(os.pread(output.fileno(), 1024 * 1024, 0).decode(errors="replace"), end="")


def exercise(root, events):
    button = find(root, "invoke")
    assert button.get_name() == "Activate"
    assert button.get_role() == Atspi.Role.PUSH_BUTTON
    assert button.get_parent() == root
    assert children(root)[button.get_index_in_parent()] == button

    field = find(root, "text")
    assert field.get_name() == "Display name"
    assert any(relation.get_relation_type() == Atspi.RelationType.LABELLED_BY
               and relation.get_target(0) == find(root, "label") for relation in field.get_relation_set())
    assert Atspi.Text.get_text(field, 0, -1) == "A\U0001f642\u4e2d"
    assert Atspi.Text.get_character_count(field) == 3
    def character_box():
        box = Atspi.Text.get_character_extents(field, 1, Atspi.CoordType.WINDOW)
        return box if box.width > 0 and box.height > 0 else None
    box = until(character_box)
    x, y = box.x + box.width // 2, box.y + box.height // 2
    assert Atspi.Text.get_offset_at_point(field, x, y, Atspi.CoordType.WINDOW) == 1
    assert Atspi.Component.get_accessible_at_point(root, x, y, Atspi.CoordType.WINDOW) == field
    screen = Atspi.Text.get_character_extents(field, 1, Atspi.CoordType.SCREEN)
    if os.environ.get("XDG_SESSION_TYPE") == "x11":
        assert screen.width == box.width and screen.height == box.height
        assert Atspi.Text.get_offset_at_point(field, screen.x + screen.width // 2,
                                              screen.y + screen.height // 2, Atspi.CoordType.SCREEN) == 1
    else:
        assert (screen.x, screen.y, screen.width, screen.height) == (-1, -1, 0, 0)
    assert Atspi.Component.grab_focus(field)
    assert Atspi.Text.set_caret_offset(field, 0)
    assert Atspi.Text.add_selection(field, 1, 2)
    selection = Atspi.Text.get_selection(field, 0)
    assert (selection.start_offset, selection.end_offset) == (1, 2)
    assert Atspi.EditableText.set_text_contents(field, "Updated \u4e2d")
    assert Atspi.Text.get_text(field, 0, -1) == "Updated \u4e2d"

    check = find(root, "check")
    assert invoke(check)
    until(lambda: has(check, "CHECKED"))
    slider = find(root, "slider")
    assert Atspi.Value.get_minimum_value(slider) == 0
    assert Atspi.Value.get_maximum_value(slider) == 10
    assert Atspi.Value.get_minimum_increment(slider) == 2
    assert Atspi.Value.set_current_value(slider, 6)
    assert Atspi.Value.get_current_value(slider) == 6
    progress = find(root, "progress")
    assert has(progress, "READ_ONLY") and Atspi.Value.get_current_value(progress) == 42

    dropdown = find(root, "dropdown")
    options = children(dropdown)
    assert [option.get_accessible_id() for option in options] == ["one", "two", "disabled"]
    assert not has(options[1], "SHOWING")
    assert invoke(dropdown)
    until(lambda: has(options[1], "SHOWING"))
    hitbox = Atspi.Component.get_extents(options[1], Atspi.CoordType.WINDOW)
    assert Atspi.Component.get_accessible_at_point(root, hitbox.x + hitbox.width // 2,
                                                  hitbox.y + hitbox.height // 2, Atspi.CoordType.WINDOW) == options[1]
    assert not Atspi.Component.grab_focus(options[2])
    assert Atspi.Selection.select_child(dropdown, 1)
    assert Atspi.Selection.get_selected_child(dropdown, 0) == options[1]
    assert invoke(button)
    until(lambda: root.get_name() == "\u7121\u969c\u7919")
    until(lambda: ("object:text-changed:insert", field) in events)
    until(lambda: ("object:text-selection-changed", field) in events)
    until(lambda: ("object:state-changed:focused", field) in events)
    until(lambda: ("object:state-changed:checked", check) in events)
    until(lambda: ("object:property-change:accessible-name", root) in events)

    old = find(root, "replaced")
    assert invoke(find(root, "remove"))
    def replacement():
        new = find(root, "replaced")
        return new if new is not None and new.get_name() == "Replacement" else None
    assert until(replacement) != old
    try:
        assert not invoke(old)
    except (GLib.Error, AttributeError):
        pass
    until(lambda: ("object:children-changed:add", root) in events)


def exercise_text_boundaries(root):
    granularity = Atspi.TextGranularity
    boundary = Atspi.TextBoundaryType
    modern = Atspi.Text.get_string_at_offset
    at = Atspi.Text.get_text_at_offset
    before = Atspi.Text.get_text_before_offset
    after = Atspi.Text.get_text_after_offset
    absent = ("", -1, -1)
    failures = []

    def check(field, method, kind, offset, expected):
        span = method(field, offset, kind)
        actual = (span.content, span.start_offset, span.end_offset)
        print(method.__name__, kind.value_nick, offset, repr(actual))
        if actual != expected:
            failures.append((method.__name__, kind.value_nick, offset, actual, expected))

    cases = [
        (modern, granularity.WORD, 0, ("First ", 0, 6)),
        (modern, granularity.WORD, 5, ("First ", 0, 6)),
        (modern, granularity.WORD, 11, ("words. ", 6, 13)),
        (modern, granularity.WORD, 28, ("sentence!", 20, 29)),
        (modern, granularity.SENTENCE, 12, ("First words. ", 0, 13)),
        (modern, granularity.SENTENCE, 13, ("Second sentence!", 13, 29)),
        (at, boundary.WORD_START, 5, ("First ", 0, 6)),
        (at, boundary.WORD_END, 5, (" words", 5, 11)),
        (at, boundary.WORD_END, 11, (". Second", 11, 19)),
        (before, boundary.WORD_START, 6, ("First ", 0, 6)),
        (before, boundary.WORD_END, 5, ("First", 0, 5)),
        (after, boundary.WORD_START, 5, ("words. ", 6, 13)),
        (after, boundary.WORD_END, 4, (" words", 5, 11)),
        (at, boundary.SENTENCE_START, 12, ("First words. ", 0, 13)),
        (at, boundary.SENTENCE_END, 11, ("First words.", 0, 12)),
        (at, boundary.SENTENCE_END, 12, (" Second sentence!", 12, 29)),
        (before, boundary.SENTENCE_END, 13, ("First words.", 0, 12)),
        (after, boundary.SENTENCE_END, 0, (" Second sentence!", 12, 29)),
        (before, boundary.SENTENCE_START, 29, ("Second sentence!", 13, 29)),
        (before, boundary.WORD_START, 0, absent),
        (after, boundary.WORD_START, 28, absent),
        (modern, granularity.WORD, -1, absent),
        (modern, granularity.SENTENCE, 29, absent),
    ]
    field = find(root, "text")
    for case in cases:
        check(field, *case)

    unicode_field = find(root, "unicode")
    assert Atspi.Text.get_character_count(unicode_field) == 15
    for case in [
        (modern, granularity.CHAR, 6, ("𠀀", 6, 7)),
        (modern, granularity.WORD, 5, ("Café,\u00a0", 0, 6)),
        (modern, granularity.WORD, 7, ("𠀀! \n", 6, 10)),
        (modern, granularity.SENTENCE, 8, ("Café,\u00a0𠀀! \n", 0, 10)),
        (modern, granularity.SENTENCE, 10, ("Last?", 10, 15)),
        (modern, granularity.LINE, 9, ("Café,\u00a0𠀀! \n", 0, 10)),
        (modern, granularity.PARAGRAPH, 9, ("Café,\u00a0𠀀! \n", 0, 10)),
        (at, boundary.LINE_START, 8, ("Café,\u00a0𠀀! \n", 0, 10)),
        (at, boundary.LINE_END, 9, ("\nLast?", 9, 15)),
        (at, boundary.SENTENCE_END, 8, (" \nLast?", 8, 15)),
    ]:
        check(unicode_field, *case)

    assert not failures, failures
    print("PASS modern and legacy text ranges")


def exercise_renderer_failure(root, host_log):
    field = find(root, "text")
    button = find(root, "invoke")
    check = find(root, "check")
    dropdown = find(root, "dropdown")
    option = children(dropdown)[1]
    slider = find(root, "slider")

    def action_names(action):
        return [action.get_action_name(i) for i in range(action.get_n_actions())]

    actions = [node.get_action_iface() for node in (button, check, dropdown, option)]
    assert all(action is not None for action in actions)
    normal = [action_names(action) for action in actions]
    assert normal == [["click"], ["toggle"], ["expand", "collapse"], ["select"]], normal
    until(lambda: has(button, "SHOWING"))
    assert Atspi.Text.get_text(field, 0, -1) == "Retained draft"
    assert invoke(button)
    until(lambda: "Renderer failed: device reset" in host_log())
    failed = [action_names(action) for action in actions]
    assert failed == [[], [], [], []], failed
    print("PASS normal role actions disappear after failure", normal, failed, flush=True)
    assert Atspi.Text.get_text(field, 0, -1) == "Retained draft"
    assert not Atspi.EditableText.set_text_contents(field, "Rejected")
    assert not Atspi.Selection.select_child(dropdown, option.get_index_in_parent())
    assert Atspi.Text.get_text(field, 0, -1) == "Retained draft"
    assert Atspi.Value.get_current_value(slider) == 2
    for action in actions:
        assert not action.do_action(0)
    print("PASS renderer failure retains readable values and rejects cached action invocations")


def exercise_queued_events(executable, modes):
    events = []
    listener = Atspi.EventListener.new(lambda event: events.append(
        (event.type, event.source, event.detail1, event.detail2, event.any_data)))
    assert listener.register("object:text-changed")
    assert listener.register("object:state-changed:showing")
    for mode in modes:
        with fixture(executable, mode):
            root = until(lambda: window(mode))
            button, field = find(root, "invoke"), find(root, "text")
            until(lambda: has(button, "SHOWING"))
            pump()
            events.clear()
            assert invoke(button)
            if mode == "queued-text":
                def edits():
                    return [(kind.rsplit(":", 1)[-1], offset, length, value)
                            for kind, source, offset, length, value in events if source == field]
                until(lambda: len(edits()) >= 4)
                assert edits() == [("delete", 2, 1, "中"), ("insert", 2, 1, "界"),
                                   ("delete", 2, 1, "界"), ("insert", 2, 1, "中")], edits()
                assert Atspi.Text.get_text(field, 0, -1) == "A\U0001f642中"
            else:
                def showing():
                    return [(source, value) for kind, source, value, _, _ in events
                            if kind == "object:state-changed:showing" and source.get_application() == root.get_application()]
                until(lambda: len(showing()) >= 4)
                transitions = showing()
                assert [value for _, value in transitions] == [1, 0, 1, 0], transitions
                first, first_closed, second, second_closed = [source for source, _ in transitions]
                assert first == first_closed and second == second_closed and first != second
                assert children(field) == []
        print("PASS", mode)


def main():
    Atspi.init()
    queued = {"--queued-events": ("queued-text", "queued-menu"),
              "--queued-text": ("queued-text",), "--queued-menu": ("queued-menu",)}
    if len(sys.argv) == 3 and sys.argv[2] in queued:
        exercise_queued_events(sys.argv[1], queued[sys.argv[2]])
        return
    if sys.argv[2:] == ["--renderer-failure"]:
        with fixture(sys.argv[1], "renderer-failure") as (host, host_log):
            exercise_renderer_failure(
                until(lambda: next((n for n in windows() if n.get_process_id() == host.pid), None)), host_log)
        return
    if sys.argv[2:] in (["--text-boundaries"], ["--text-boundaries-headless"]):
        with fixture(sys.argv[1], sys.argv[2][2:]):
            exercise_text_boundaries(until(lambda: window("text-boundaries")))
        return
    events = []
    listener = Atspi.EventListener.new(lambda event: events.append((event.type, event.source)))
    assert listener.register("object")
    for mode in ("default", "enabled"):
        events.clear()
        with fixture(sys.argv[1], mode) as (host, host_log):
            root = until(lambda: window(mode))
            exercise(root, events)
            assert invoke(find(root, "close"))
            assert host.wait(timeout=8) == 0
            assert "Model 1 6 two 1" in host_log()
            until(lambda: root not in list(windows()))
        print("PASS native discovery, roles, relations, geometry, Unicode text, actions, events, stale references and", mode)

    with fixture(sys.argv[1], "disabled") as (host, host_log):
        until(lambda: "Disabled host dispatched local focus, text and structure" in host_log())
        assert not list(windows())
        assert host.poll() is None
    with fixture(sys.argv[1], "dual") as (host, host_log):
        until(lambda: "Disabled host dispatched local focus, text and structure" in host_log())
        mapped = until(lambda: window("enabled"))
        assert list(windows()) == [mapped]
        assert invoke(find(mapped, "invoke"))
        until(lambda: mapped.get_name() == "\u7121\u969c\u7919")
        assert host.poll() is None
    print("PASS explicit disable and same-process host isolation")


if __name__ == "__main__":
    main()
