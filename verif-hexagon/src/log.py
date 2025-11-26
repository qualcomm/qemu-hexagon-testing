#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from inspect import currentframe, getframeinfo
import os, sys

workarounds = set()
verbosity_level = 1
DEBUG, INFO, WARNING, CRITICAL = 3, 2, 1, 0
YELLOW = "\x1b[33;20m"
RED = "\x1b[31;20m"
RESET = "\x1b[0m"

def print_colored(color, msg):
    if sys.stdout.isatty():
        print(f"{color}{msg}{RESET}")
    else:
        print(msg)

def config(level):
    global verbosity_level
    verbosity_level = level

def workaround(issue_id):
    if issue_id not in workarounds:
        frame = currentframe().f_back
        lineno = frame.f_lineno
        filename = os.path.basename(getframeinfo(frame).filename)
        warn(f"employing workaround for {issue_id} on {filename}:{lineno}")
        workarounds.add(issue_id)

def warn(msg):
    if verbosity_level >= WARNING:
        print_colored(YELLOW, f"WARN: {msg}")

def debug(msg):
    if verbosity_level >= DEBUG:
        print(f"DEBUG: {msg}")

def info(msg):
    if verbosity_level >= INFO:
        print(f"INFO: {msg}")

def critical(msg):
    if verbosity_level >= CRITICAL:
        print_colored(RED, f"ERROR: {msg}")
        sys.exit(1)

def progress_bar(title, N):
    if verbosity_level >= 1:
        import progressbar
        widgets = [
            f"{title}: ",
            progressbar.SimpleProgress(),
            ' (', progressbar.Percentage(), ') ',
            progressbar.Bar(
                marker=progressbar.AnimatedMarker(
                    fill='█',
                    fill_wrap='\x1b[32m{}\x1b[39m',
                )
            ),
            ' ',
            progressbar.ETA(),
        ]
        return progressbar.ProgressBar(max_value=N, redirect_stdout=True,
                                       widgets=widgets)
    else:
        class MockupProgress():
            def update(self, i):
                pass
        return MockupProgress()
