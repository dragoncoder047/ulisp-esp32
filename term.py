#! /usr/bin/env python3
from sys import exit
from serial import Serial, SerialException
from argparse import ArgumentParser
from prompt_toolkit import PromptSession, Application
from prompt_toolkit.layout import VSplit, HSplit, BufferControl, Window
from prompt_toolkit.buffer import Buffer
from prompt_toolkit.widgets import TextArea, Label, VerticalLine
from prompt_toolkit.layout import Layout, ScrollablePane
from prompt_toolkit.history import FileHistory
from prompt_toolkit.auto_suggest import AutoSuggestFromHistory
from prompt_toolkit.validation import Validator, ValidationError
from prompt_toolkit.lexers import PygmentsLexer
from prompt_toolkit.formatted_text import HTML
from prompt_toolkit.patch_stdout import patch_stdout
from prompt_toolkit.shortcuts import set_title
from pygments.lexers.lisp import CommonLispLexer
import re
import os.path
import asyncio


WORKSPACESIZE = 9216 - 172
FREE = WORKSPACESIZE

input_queue = asyncio.Queue()

HEADER_RE = re.compile(r"uLisp ([\d.ab]+)", re.M)
PROMPT_RE = re.compile(r"(\d+)> ", re.M)


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


def submit_box(b: Buffer):
    input_queue.put_nowait(b.document.text)
    return False


def mem_usage_indicator():
    width = app.output.get_size().columns
    usage_percent = 1 - FREE / WORKSPACESIZE
    s = f"{FREE} free ({round(usage_percent * 100, 2)}%) ["
    e = "]"
    if usage_percent > 0.75:
        color = "#600"
    elif usage_percent > 0.5:
        color = "#630"
    else:
        color = "#040"
    bw = width - len(s) - len(e)
    nb = round(bw * usage_percent)
    bar = "#" * nb + " " * (bw - nb)
    return HTML(f"""<style bg="{color}">{s}{bar}{e}</style>""")


lispbuffer = TextArea(
    multiline=True,
    lexer=PygmentsLexer(CommonLispLexer),
    focus_on_click=True,
    scrollbar=True,
    validator=LispValidator(),
)
terminal = TextArea(
    read_only=True,
    scrollbar=True,
)
cmdarea = TextArea(
    scrollbar=False,
    wrap_lines=False,
    lexer=PygmentsLexer(CommonLispLexer),
    height=1,
    focus_on_click=True,
    validator=LispValidator(),
    accept_handler=submit_box,
    multiline=False,
)
app = Application(Layout(HSplit([
    VSplit([
        lispbuffer,
        VerticalLine(),
        terminal
    ]),
    VSplit([
        Label(text="cmd> ", dont_extend_width=True, dont_extend_height=True),
        cmdarea,
    ]),
    Label(text=mem_usage_indicator, dont_extend_height=True)
])), mouse_support=True, full_screen=True)


def output(s: str):
    if not s:
        return
    terminal.text += s.replace("\r\n", "\n")
    for _ in range(s.count("\n")):
        terminal.control.move_cursor_down()


def parse_prompt(prompt: str) -> int:
    if m := PROMPT_RE.search(prompt):
        return int(m.group(1))
    return 0


def passthrough_until_prompt(port: Serial) -> str:
    out = ""
    while True:
        line = port.read_until(b"\n").decode()
        if line:
            out += line
        if PROMPT_RE.search(out) is not None:
            return out
        output(line)


def startup(port: Serial) -> str:
    set_title(f"uLisp on {port.port}")
    port.reset_input_buffer()
    port.dtr = False
    port.dtr = True
    header = passthrough_until_prompt(port)
    if m := HEADER_RE.search(header):
        ver = " " + m.group(1)
    else:
        ver = ""
    set_title(f"uLisp{ver} on {port.port}")
    return header.rsplit("\n", 1)[-1]


async def repl(port: Serial):
    global FREE
    with patch_stdout():
        prompt = startup(port)
        while True:
            FREE = parse_prompt(prompt)
            send = await input_queue.get()
            port.write(send.encode())
            port.write(b"\n")
            port.flush()
            prompt = passthrough_until_prompt(port).rsplit("\n", 1)[-1]


async def main():
    a = ArgumentParser("term.py")
    a.add_argument("-p", "--port", default="/dev/ttyUSB0")
    a.add_argument("-b", "--baud", default=115200)
    x = a.parse_args()
    try:
        port = Serial(x.port, x.baud, timeout=0.1, exclusive=True)
    except SerialException:
        exit(f"error: could not open {x.port}\n"
             "- device plugged in?\n"
             "- wrong port? (specify with -p PORT)\n")

    tasks = []
    tasks.append(app.run_async())
    tasks.append(repl(port))
    await asyncio.gather(*tasks, return_exceptions=True)


if __name__ == '__main__':
    asyncio.run(main())
