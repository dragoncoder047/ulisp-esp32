#! /usr/bin/env python3
from serial import Serial, SerialException
from argparse import ArgumentParser
from prompt_toolkit import Application
from prompt_toolkit.layout import VSplit, HSplit
from prompt_toolkit.buffer import Buffer
from prompt_toolkit.widgets import (
    TextArea, Label, VerticalLine, HorizontalLine)
from prompt_toolkit.layout import Layout
from prompt_toolkit.history import InMemoryHistory
from prompt_toolkit.auto_suggest import AutoSuggestFromHistory
from prompt_toolkit.validation import Validator, ValidationError
from prompt_toolkit.lexers import PygmentsLexer
from prompt_toolkit.formatted_text import HTML
from prompt_toolkit.shortcuts import set_title
from pygments.lexers.lisp import CommonLispLexer
import re
import asyncio


input_queue = asyncio.Queue()


class LispValidator(Validator):
    def validate(self, document):
        nesting = 0
        stringmode = False
        for c in document.text:
            if c == '"':
                stringmode = not stringmode
            elif c == "(" and not stringmode:
                nesting += 1
            elif c == ")" and not stringmode:
                nesting -= 1
        if nesting > 0:
            raise ValidationError(len(document.text), "Unbalanced parens")
        elif stringmode:
            raise ValidationError(len(document.text), "Unclosed string")


class Watcher:
    all_watchers = []

    def __init__(self, regex):
        self.regex = re.compile(regex, re.M)
        Watcher.all_watchers.append(self)

    def __call__(self, fun):
        self.fun = fun

    def run(self, content: str) -> str:
        if m := self.regex.search(content):
            if self.fun(m):
                content = content.replace(m.group(0), "", 1)
        return content


def run_watchers(content: str) -> str:
    changed = True
    while changed:
        changed = False
        for w in Watcher.all_watchers:
            old = content
            content = w.run(content)
            if content != old:
                changed = True
    return content


WORKSPACESIZE = 1
FREE = 0
FREED = 0
GC_COUNTER = 0
LAST_ERROR = ""
STATUS = "Loading..."
RIGHT_STATUS = ""


@Watcher(r"\{GC#(\d+):(\d+),(\d+)/(\d+)\}")
def mem_usage_watcher(m: re.Match):
    global GC_COUNTER
    global FREED
    global FREE
    global WORKSPACESIZE
    GC_COUNTER = int(m.group(1))
    FREED = int(m.group(2))
    FREE = int(m.group(3))
    WORKSPACESIZE = int(m.group(4))
    return True


@Watcher(r"\[Ready.\]\n")
def ready_watcher(m: re.Match):
    global STATUS
    if "error" not in STATUS.lower():
        STATUS = "Ready."
    return True


@Watcher(r"\$!rs=(.*)!\$\n?")
def right_status_watcher(m: re.Match):
    global RIGHT_STATUS
    RIGHT_STATUS = m.group(1)
    return True


@Watcher(r"waiting for download")
def bootloader_watcher(m: re.Match):
    raise SerialException("Device is in bootloader mode")


@Watcher(r"(Error: [^\n]+)\n")
def error_watcher(m: re.Match):
    global STATUS
    STATUS = m.group(1)
    return True


def memory_usage_bar():
    width = app.output.get_size().columns
    usage_percent = 1 - FREE / WORKSPACESIZE
    s = f"{FREE}/{WORKSPACESIZE} free ({round(usage_percent * 100, 2)}%) ["
    e = f"] (GC #{GC_COUNTER} freed {FREED})"
    if usage_percent > 0.75:
        color = "#F78"
    elif usage_percent > 0.5:
        color = "#E90"
    else:
        color = "#0B3"
    bw = width - len(s) - len(e)
    nb = round(bw * usage_percent)
    bar = "#" * nb + " " * (bw - nb)
    return HTML(f"""<style bg="{color}" fg="#000">{s}{bar}{e}</style>""")


def status_bar():
    width = app.output.get_size().columns
    left = STATUS
    right = RIGHT_STATUS
    spaces = width - len(right) - len(left)
    return HTML((left + " " * spaces + right).rstrip("\r\n"))


def submit_box(b: Buffer):
    input_queue.put_nowait(b.document.text)
    return False


lispbuffer = TextArea(
    multiline=True,
    lexer=PygmentsLexer(CommonLispLexer),
    focus_on_click=True,
    scrollbar=True,
    validator=LispValidator(),
    history=InMemoryHistory(),
    auto_suggest=AutoSuggestFromHistory())

terminal = TextArea(
    read_only=True,
    scrollbar=True)

command_bar = TextArea(
    scrollbar=False,
    wrap_lines=False,
    lexer=PygmentsLexer(CommonLispLexer),
    height=2,
    focus_on_click=True,
    validator=LispValidator(),
    accept_handler=submit_box,
    multiline=False,
    history=InMemoryHistory(),
    auto_suggest=AutoSuggestFromHistory())

app = Application(Layout(HSplit([
    VSplit([
        lispbuffer,
        VerticalLine(),
        terminal
    ]),
    HorizontalLine(),
    VSplit([
        Label(text="cmd> ", dont_extend_width=True, dont_extend_height=True),
        command_bar,
    ]),
    HorizontalLine(),
    Label(text=status_bar, dont_extend_height=True),
    Label(text=memory_usage_bar, dont_extend_height=True)
])), mouse_support=True, full_screen=True)


def output(s: str = ""):
    terminal.text += s
    terminal.text = terminal.text.replace("\r\n", "\n")
    terminal.buffer.cursor_position = len(terminal.text)


def startup(port: Serial) -> str:
    set_title(f"uLisp on {port.port} ({port.name})")
    port.reset_input_buffer()
    port.dtr = False
    port.dtr = True
    output("\n---MCU RESET---\n")


async def repl_task(port: Serial):
    global STATUS
    startup(port)
    await asyncio.sleep(0.1)
    while True:
        # allow other tasks to run
        await asyncio.sleep(0.1)
        if not input_queue.empty():
            send = await input_queue.get()
            match send:
                case ".reset":
                    startup(port)
                    send = None
                case ".quit":
                    app.exit()
                    return
                case ".run":
                    send = lispbuffer.text
                    lispbuffer.buffer.append_to_history()
                    lispbuffer.text = ""
                case _:
                    pass
            if send is not None and send.strip():
                STATUS = "Running..."
                port.write(send.encode())
                port.write(b"\n")
                port.flush()
            input_queue.task_done()
        if port.in_waiting > 0:
            terminal.text += port.read_all().decode()
            terminal.text = run_watchers(terminal.text)
            output()


async def main():
    argp = ArgumentParser("term.py")
    argp.add_argument("-p", "--port", default="/dev/ttyUSB0")
    argp.add_argument("-b", "--baud", default=115200)
    foo = argp.parse_args()
    port = Serial(foo.port, foo.baud, timeout=0.1, exclusive=True)

    @Watcher(r"uLisp ([\d.a-z]+)")
    def version_watcher(m: re.Match):
        nonlocal port
        set_title(f"uLisp {m.group(1)} on {port.port} ({port.name})")

    await asyncio.gather(
        app.run_async(),
        repl_task(port))


if __name__ == '__main__':
    asyncio.run(main())
