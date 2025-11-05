#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

from inspect import currentframe, getframeinfo
import os
import logging

workarounds = set()
VERBOSITY_LEVELS = [logging.CRITICAL, logging.WARNING, logging.INFO, logging.DEBUG]
verbosity_level = 1

def config(level):
    global verbosity_level
    verbosity_level = level
    logging.basicConfig(level=VERBOSITY_LEVELS[level])

def workaround(issue_id):
    if verbosity_level >= 1 and issue_id not in workarounds:
        frame = currentframe().f_back
        lineno = frame.f_lineno
        filename = os.path.basename(getframeinfo(frame).filename)
        print(f"Warn: employing workaround for {issue_id} on {filename}:{lineno}")
        workarounds.add(issue_id)

warn = logging.warn
debug = logging.debug
info = logging.info
critical = logging.critical

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
