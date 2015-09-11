#!/usr/bin/env python2
#
# Copyright (c) 2015 Intel Corporation
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

import xml.etree.ElementTree as ET
import argparse
import sys

symbol_to_perf_map = { 'RenderBasic' : '3D',
                       'ComputeBasic' : 'COMPUTE',
                       'ComputeExtended': 'COMPUTE_EXTENDED',
                       'MemoryReads': 'MEMORY_READS',
                       'MemoryWrites': 'MEMORY_WRITES',
                       'SamplerBalance': 'SAMPLER_BALANCE' }

def print_err(*args):
    sys.stderr.write(' '.join(map(str,args)) + '\n')

c_file = None
_c_indent = 0

def c(*args):
    if c_file:
        code = ' '.join(map(str,args))
        for line in code.splitlines():
            text = ''.rjust(_c_indent) + line
            c_file.write(text.rstrip() + "\n")

#without indenting or new lines
def c_frag(*args):
    code = ' '.join(map(str,args))
    c_file.write(code)

def c_indent(n):
    global _c_indent
    _c_indent = _c_indent + n
def c_outdent(n):
    global _c_indent
    _c_indent = _c_indent - n

header_file = None
_h_indent = 0

def h(*args):
    if header_file:
        code = ' '.join(map(str,args))
        for line in code.splitlines():
            text = ''.rjust(_h_indent) + line
            header_file.write(text.rstrip() + "\n")

def h_indent(n):
    global _c_indent
    _h_indent = _h_indent + n
def h_outdent(n):
    global _c_indent
    _h_indent = _h_indent - n


def emit_fadd(tmp_id, args):
    c("double tmp" + str(tmp_id) +" = " + args[1] + " + " + args[0] + ";")
    return tmp_id + 1

# Be careful to check for divide by zero...
def emit_fdiv(tmp_id, args):
    c("double tmp" + str(tmp_id) +" = " + args[1] + ";")
    c("double tmp" + str(tmp_id + 1) +" = " + args[0] + ";")
    c("double tmp" + str(tmp_id + 2) +" = tmp" + str(tmp_id + 1)  + " ? tmp" + str(tmp_id) + " / tmp" + str(tmp_id + 1) + " : 0;")
    return tmp_id + 3

def emit_fmax(tmp_id, args):
    c("double tmp" + str(tmp_id) +" = " + args[1] + ";")
    c("double tmp" + str(tmp_id + 1) +" = " + args[0] + ";")
    c("double tmp" + str(tmp_id + 2) +" = MAX(tmp" + str(tmp_id) + ", tmp" + str(tmp_id + 1) + ");")
    return tmp_id + 3

def emit_fmul(tmp_id, args):
    c("double tmp" + str(tmp_id) +" = " + args[1] + " * " + args[0] + ";")
    return tmp_id + 1

def emit_fsub(tmp_id, args):
    c("double tmp" + str(tmp_id) +" = " + args[1] + " - " + args[0] + ";")
    return tmp_id + 1

def emit_read(tmp_id, args):
    type = args[1].lower()
    c("uint64_t tmp" + str(tmp_id) + " = accumulator[query->" + type + "_offset + " + args[0] + "];")
    return tmp_id + 1

def emit_uadd(tmp_id, args):
    c("uint64_t tmp" + str(tmp_id) +" = " + args[1] + " + " + args[0] + ";")
    return tmp_id + 1

# Be careful to check for divide by zero...
def emit_udiv(tmp_id, args):
    c("uint64_t tmp" + str(tmp_id) +" = " + args[1] + ";")
    c("uint64_t tmp" + str(tmp_id + 1) +" = " + args[0] + ";")
    c("uint64_t tmp" + str(tmp_id + 2) +" = tmp" + str(tmp_id + 1)  + " ? tmp" + str(tmp_id) + " / tmp" + str(tmp_id + 1) + " : 0;")
    return tmp_id + 3

def emit_umul(tmp_id, args):
    c("uint64_t tmp" + str(tmp_id) +" = " + args[1] + " * " + args[0] + ";")
    return tmp_id + 1

def emit_usub(tmp_id, args):
    c("uint64_t tmp" + str(tmp_id) +" = " + args[1] + " - " + args[0] + ";")
    return tmp_id + 1

ops = {}
# n operands, type, emitter
ops["FADD"] = (2, emit_fadd)
ops["FDIV"] = (2, emit_fdiv)
ops["FMAX"] = (2, emit_fmax)
ops["FMUL"] = (2, emit_fmul)
ops["FSUB"] = (2, emit_fsub)
ops["READ"] = (2, emit_read)
ops["UADD"] = (2, emit_uadd)
ops["UDIV"] = (2, emit_udiv)
ops["UMUL"] = (2, emit_umul)
ops["USUB"] = (2, emit_usub)

hw_vars = {}
hw_vars["$EuCoresTotalCount"] = "devinfo->n_eus"
hw_vars["$EuSlicesTotalCount"] = "devinfo->n_eu_slices"
hw_vars["$EuSubslicesTotalCount"] = "devinfo->n_eu_sub_slices"
hw_vars["$SubsliceMask"] = "devinfo->subslice_mask"

counter_vars = {}

def output_rpn_equation_code(set, counter, equation, counter_vars):
    c("/* RPN equation: " + equation + " */")
    tokens = equation.split()
    stack = []
    tmp_id = 0
    tmp = None

    for token in tokens:
        stack.append(token)
        while stack and stack[-1] in ops:
            op = stack.pop()
            argc, callback = ops[op]
            args = []
            for i in range(0, argc):
                operand = stack.pop()
                if operand[0] == "$":
                    if operand in hw_vars:
                        operand = hw_vars[operand]
                    elif operand in counter_vars:
                        reference = counter_vars[operand]
                        operand = read_funcs[operand[1:]] + "(devinfo, query, accumulator)"
                    else:
                        raise Exception("Failed to resolve variable " + operand + " in equation " + equation + " for " + set.get('name') + " :: " + counter.get('name'));
                args.append(operand)

            tmp_id = callback(tmp_id, args)

            tmp = "tmp" + str(tmp_id - 1)
            stack.append(tmp)

    if tmp_id == 0:
        raise Exception("Spurious empty rpn code for " + set.get('name') + " :: " +
                counter.get('name') + ".\nThis is probably due to some unhandled RPN function, in the equation \"" +
                counter.get('equation') + "\"")

    c("\nreturn tmp" + str(tmp_id - 1) + ";")

def output_counter_read(set, counter, counter_vars):
    c("\n")
    c("/* " + set.get('name') + " :: " + counter.get('name') + " */")
    ret_type = counter.get('data_type')
    if ret_type == "uint64":
        ret_type = "uint64_t"

    c("static " + ret_type)
    read_sym = set.get('chipset').lower() + "__" + set.get('underscore_name') + "__" + counter.get('underscore_name') + "__read"
    c(read_sym + "(struct gputop_devinfo *devinfo,\n")
    c_indent(len(read_sym) + 1)
    c("const struct gputop_perf_query *query,\n")
    c("uint64_t *accumulator)\n")
    c_outdent(len(read_sym) + 1)

    c("{")
    c_indent(3)

    output_rpn_equation_code(set, counter, counter.get('equation'), counter_vars)

    c_outdent(3)
    c("}")

    return read_sym


def output_counter_max(set, counter, counter_vars):
    max_eq = counter.get('max_equation')

    if not max_eq:
        return "NULL; /* undefined */"

    if max_eq == "100":
        return "percentage_max_callback;"

    c("\n")
    c("/* " + set.get('name') + " :: " + counter.get('name') + " */")
    ret_type = counter.get('data_type')
    if ret_type == "uint64":
        ret_type = "uint64_t"

    c("static " + ret_type)
    max_sym = set.get('chipset').lower() + "__" + set.get('underscore_name') + "__" + counter.get('underscore_name') + "__max"
    c(max_sym + "(struct gputop_devinfo *devinfo,\n")
    c_indent(len(max_sym) + 1)
    c("const struct gputop_perf_query *query,\n")
    c("uint64_t *accumulator)\n")
    c_outdent(len(max_sym) + 1)

    c("{")
    c_indent(3)

    output_rpn_equation_code(set, counter, max_eq, counter_vars)

    c_outdent(3)
    c("}")

    return max_sym + ";"


semantic_type_map = {
    "duration": "raw",
    "ratio": "event"
    }

def output_counter_report(set, counter):
    data_type = counter.get('data_type')
    data_type_uc = data_type.upper()
    c_type = data_type

    if "uint" in c_type:
        c_type = c_type + "_t"

    semantic_type = counter.get('semantic_type')
    if semantic_type in semantic_type_map:
        semantic_type = semantic_type_map[semantic_type]

    semantic_type_uc = semantic_type.upper()

    c("\n");

    conditions = counter.findall("./condition")
    if len(conditions):
        if len(conditions) > 1:
            sys.exit("FIXME: multiple conditions per counter not handled");

        cond = conditions[0].get('equation')
        tokens = cond.split()
        if len(tokens) != 3 or tokens[2] != "AND" or tokens[0][0] != '$':
            sys.exit("FIXME: couldn't handle counter condition" + cond);

        if tokens[0] not in hw_vars:
            sys.exit("FIXME: failed to look up variable in counter condition: " + cond)

        c("if (" + hw_vars[tokens[0]] + " & " + tokens[1] + ") {\n");
        c_indent(4)


    c("counter = &query->counters[query->n_counters++];\n")
    c("counter->oa_counter_read_" + data_type + " = " + read_funcs[counter.get('symbol_name')] + ";\n")
    c("counter->name = \"" + counter.get('name') + "\";\n")
    c("counter->desc = \"" + counter.get('description') + "\";\n")
    c("counter->type = GPUTOP_PERFQUERY_COUNTER_" + semantic_type_uc + ";\n")
    c("counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_" + data_type_uc + ";\n")
    c("counter->max = " + max_funcs[counter.get('symbol_name')] + "\n")

    if len(conditions):
        c_outdent(4)
        c("}\n")


parser = argparse.ArgumentParser()
parser.add_argument("xml", help="XML description of metrics")
parser.add_argument("--header", help="Header file to write")
parser.add_argument("--code", help="C file to write")
parser.add_argument("--include", help="Header name to include from C file")

args = parser.parse_args()

if args.header:
    header_file = open(args.header, 'w')

if args.code:
    c_file = open(args.code, 'w')

tree = ET.parse(args.xml)


copyright = """/* Autogenerated file, DO NOT EDIT manually!
 *
 * Copyright (c) 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

"""

h(copyright)
h("""#pragma once

#include "gputop-perf.h"

""")

c(copyright)
c(
"""
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

""")

if args.include:
    c("#include \"" + args.include + "\"")

c(
"""
#include "gputop-util.h"
#include "gputop-perf.h"


static uint64_t
percentage_max_callback(struct gputop_devinfo *devinfo,
                        const struct gputop_perf_query *query,
                        uint64_t *accumulator)
{
   return 100;
}

""")

for set in tree.findall(".//set"):
    max_funcs = {}
    read_funcs = {}
    counter_vars = {}
    counters = set.findall("counter")
    chipset = set.get('chipset').lower()

    for counter in counters:
        empty_vars = {}
        read_funcs[counter.get('symbol_name')] = output_counter_read(set, counter, counter_vars)
        max_funcs[counter.get('symbol_name')] = output_counter_max(set, counter, counter_vars)
        counter_vars["$" + counter.get('symbol_name')] = counter

    h("void gputop_oa_add_" + set.get('underscore_name') + "_counter_query_" + chipset + "(struct gputop_devinfo *devinfo);\n")

    c("\nvoid\n")
    c("gputop_oa_add_" + set.get('underscore_name') + "_counter_query_" + chipset + "(struct gputop_devinfo *devinfo)\n")
    c("{\n")
    c_indent(3)

    perf_suffix = symbol_to_perf_map[set.get('symbol_name')]

    c("struct gputop_perf_query *query;\n")
    c("struct gputop_perf_query_counter *counter;\n\n")

    c("query = &perf_queries[I915_OA_METRICS_SET_" + perf_suffix + "];\n")
    c("query->name = \"" + set.get('name') + "\";\n")
    c("query->counters = xmalloc0(sizeof(struct gputop_perf_query_counter) * " + str(len(counters)) + ");\n")
    c("query->n_counters = 0;\n")
    c("query->perf_oa_metrics_set = I915_OA_METRICS_SET_" + perf_suffix + ";\n")

    if chipset == "hsw":
        c("""query->perf_oa_format = I915_OA_FORMAT_A45_B8_C8;

query->perf_raw_size = 256;
query->gpu_time_offset = 0;
query->a_offset = 1;
query->b_offset = query->a_offset + 45;
query->c_offset = query->b_offset + 8;

""")
    else:
        c("""query->perf_oa_format = I915_OA_FORMAT_A32u40_A4u32_B8_C8;

query->perf_raw_size = 256;
query->gpu_time_offset = 0;
query->gpu_clock_offset = 1;
query->a_offset = 2;
query->b_offset = query->a_offset + 36;
query->c_offset = query->b_offset + 8;

""")

    for counter in counters:
        output_counter_report(set, counter)

    c_outdent(3)
    c("}\n")

