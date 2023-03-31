#! /usr/bin/env python3
from sys import exit
from serial import Serial, SerialException
from argparse import ArgumentParser
from prompt_toolkit import PromptSession, Application
from prompt_toolkit.layout import VSplit, HSplit, BufferControl, Window
from prompt_toolkit.buffer import Buffer
from prompt_toolkit.widgets import TextArea, Button, VerticalLine
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


WORKSPACESIZE = 9216 - 172
HEADER_RE = re.compile(r"uLisp (\d+)", re.M)
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


def parse_prompt(prompt: str) -> int:
    return int(PROMPT_RE.match(prompt) or "0")


def mem_usage_indicator(num_used: int, ps: PromptSession):
    width = ps.output.get_size().columns
    usage_percent = num_used / WORKSPACESIZE
    s = f"{num_used}/{WORKSPACESIZE} ({usage_percent * 100})% ["
    e = "]"
    if usage_percent > 0.75:
        color = "ansired"
    elif usage_percent > 0.5:
        color = "ansiyellow"
    else:
        color = "ansiblue"
    bw = width - len(s) - len(e)
    nb = round(bw * usage_percent)
    bar = "#" * nb + " " * (bw - nb)
    return HTML(f"""<style bg="{color}">{s}{bar}{e}</style>""")


def get_lisp_input(prompt: str, ps: PromptSession) -> str:
    num_used = parse_prompt(prompt)
    try:
        string = ps.prompt(
            "uLisp> ",
            multiline=True,
            lexer=PygmentsLexer(CommonLispLexer),
            auto_suggest=AutoSuggestFromHistory(),
            bottom_toolbar=lambda: mem_usage_indicator(num_used, ps))
    except EOFError:
        print("^D")
        exit(0)
    return string


def passthrough_until_prompt(port: Serial) -> str:
    out = ""
    while True:
        line = port.read_until(b"\n").decode()
        if line:
            out += line
        if PROMPT_RE.search(out) is not None:
            return out
        print(line, end="", flush=True)


def startup(port: Serial) -> str:
    set_title(f"uLisp on {port.port}")
    port.reset_input_buffer()
    port.dtr = False
    port.dtr = True
    header = passthrough_until_prompt(port)
    if m := HEADER_RE.match(header):
        ver = " " + m.group(1)
    else:
        ver = ""
    set_title(f"uLisp{ver} on {port.port}")
    return header.rsplit("\n", 1)[-1]


async def repl(port: Serial, ps: PromptSession):
    with patch_stdout():
        prompt = startup(port)
        while True:
            send = get_lisp_input(prompt, ps)
            port.write(send.encode())
            port.write(b"\n")
            port.flush()
            prompt = passthrough_until_prompt(port).rsplit("\n", 1)[-1]


def main():
    a = ArgumentParser("term.py")
    a.add_argument("-p", "--port", default="/dev/ttyUSB0")
    a.add_argument("-b", "--baud", default=115200)
    a.add_argument("-r", "--histfile", default="~/.ulisp_history")
    x = a.parse_args()
    try:
        port = Serial(x.port, x.baud, timeout=0.1, exclusive=True)
    except SerialException as e:
        exit(repr(e))
    x.histfile = os.path.expanduser(x.histfile)
    if not os.path.exists(x.histfile):
        with open(x.histfile, "w"):
            pass

    lispbuffer = TextArea(
        multiline=True,
        lexer=PygmentsLexer(CommonLispLexer),
        history=FileHistory(x.histfile),
        focus_on_click=True,
        auto_suggest=AutoSuggestFromHistory(),
        
        scrollbar=True,
        validator=LispValidator(),
    )
    terminal = TextArea(
        read_only=True,
        scrollbar=True,
        wrap_lines=False,
    )
    terminal.text = "foofoo"
    app = Application(Layout(HSplit([
        VSplit([
            Button(text="Quit", handler=lambda: app.exit(),
                   left_symbol="[", right_symbol="]"),
            Button(text="Break", handler=lambda: port.write(b"~"),
                   left_symbol="[", right_symbol="]"),
            Button(text="Reboot", handler=None,
                   left_symbol="[", right_symbol="]")
        ]),
        VSplit([
            lispbuffer,
            VerticalLine(),
            terminal
        ]),
    ])), mouse_support=True, full_screen=True)
    app.run()


if __name__ == '__main__':
    main()
