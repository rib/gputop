#!/usr/bin/env python2
#
# Copyright (c) 2016 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

import os
import os.path as path
import glob
import subprocess

srcdir = path.dirname(path.realpath(__file__))
top_srcdir = path.dirname(srcdir)
scriptdir = path.join(top_srcdir, 'scripts')

for xml in glob.glob(path.join(srcdir, 'oa-*.xml')):
    filename = path.basename(xml)
    arch = filename[3:-4];
    cmd = [path.join(scriptdir, 'gputop-oa-codegen.py'),
                     '--chipset=' + arch,
                     '--xml-out=' + arch + '.xml',
                     xml ]
    print("(GENXML) " + " ".join(cmd[2:]))
    ret = subprocess.call(cmd)
    if ret != 0:
        process.exit(ret)
