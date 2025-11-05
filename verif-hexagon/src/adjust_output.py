#!/usr/bin/env python3
#
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import re

def adjust_output(text):
    text = re.sub(r'\(lldb\) settings set -- target.run-args.*', '', text)
    text = re.sub(r' *\(lldb\).*', '', text)
    text = re.sub(r' *[Qq][Ee][Mm][Uu].* exe path set from.*', '', text)
    text = re.sub(r", name = '[^']+'", "", text)
    text = re.sub(r'r0(\d) =', r' r\1 =', text)

    def line_filter(line):
        return (len(line) > 0 and line != "Done!" and
                not re.match(r"^\tT.*", line) and
                not re.match(r"^Process [0-9]* (exited|stopped).*", line))
    text = "\n".join(filter(line_filter, text.split("\n")))

    return text

def adjust_binary_output(b):
    text = b.decode('iso-8859-1')
    text = adjust_output(text)
    return text.encode('iso-8859-1')

if __name__ == "__main__":
    import sys
    with open(sys.argv[1]) as f:
        contents = f.read()
    contents = adjust_output(contents)
    with open(sys.argv[1], "w") as f:
        f.write(contents)
