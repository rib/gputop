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
                       'ComputeBasic' : 'COMPUTE' }

def print_err(*args):
    sys.stderr.write(' '.join(map(str,args)) + '\n')

c_file = None
xml_equations = None
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


def check_operand_type(arg):
    if arg.isdigit():
        return "\n<mn>" + arg + "</mn>"
    elif arg[0] == "$":
        return "\n<maction actiontype='tooltip'>\n<mi>" + arg + "</mi>\n<mtext>placeholder</mtext>\n</maction>"
    return arg

mul_precedence = 2
add_precedence = 1
sub_precedence = 1
default_precedence = 10 #a high value which denotes no brackets needed

def put_brackets(arg):
    return "\n<mtext>(</mtext>" + arg + "\n<mtext>)</mtext>"

def mathml_splice_add(args):
    operand_0 = check_operand_type(args[0][0])
    operand_1 = check_operand_type(args[1][0])
    if args[0][1] < add_precedence:
        operand_0 = put_brackets(args[0][0])
    if args[1][1] < add_precedence:
        operand_1 = put_brackets(args[1][0])
    return [operand_1 + "\n<mo>+</mo>" + operand_0, add_precedence]

def mathml_splice_div(args):
    operand_0 = check_operand_type(args[0][0])
    operand_1 = check_operand_type(args[1][0])
    return ["\n<mfrac>\n<mrow>" + operand_1 + "\n</mrow>\n<mrow>" + operand_0 + "</mrow>\n</mfrac>", default_precedence]

def mathml_splice_max(args):
    operand_0 = check_operand_type(args[0][0])
    operand_1 = check_operand_type(args[1][0])
    return ["\n<mtext>max ( </mtext>" + operand_1 + "\n<mtext> , </mtext>" + operand_0 + "\n<mtext> ) </mtext>", default_precedence]

def mathml_splice_mul(args):
    operand_0 = check_operand_type(args[0][0])
    operand_1 = check_operand_type(args[1][0])
    if args[0][1] < mul_precedence:
        operand_0 = put_brackets(args[0][0])
    if args[1][1] < mul_precedence:
        operand_1 = put_brackets(args[1][0])
    return [operand_1 + "\n<mo>*</mo>" + operand_0, mul_precedence]

def mathml_splice_sub(args):
    operand_0 = check_operand_type(args[0][0])
    operand_1 = check_operand_type(args[1][0])
    if args[0][1] < sub_precedence:
        operand_0 = put_brackets(args[0][0])
    if args[1][1] < sub_precedence:
        operand_1 = put_brackets(args[1][0])
    return [operand_1 + "\n<mo>-</mo>" + operand_0, sub_precedence]

def mathml_splice_read(args):
    return ["\n<maction actiontype='tooltip'>\n<mi>" + args[1][0] + args[0][0] + "</mi>\n<mtext>placeholder</mtext>\n</maction>", default_precedence]

def mathml_splice_min(args):
    operand_0 = check_operand_type(args[0][0])
    operand_1 = check_operand_type(args[1][0])
    return ["\n<mtext>min ( </mtext>" + operand_1 + "\n<mtext> , </mtext>" + operand_0 + "\n<mtext> ) </mtext>", default_precedence]

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
    c("uint64_t tmp" + str(tmp_id) + " = accumulator[metric_set->" + type + "_offset + " + args[0] + "];")
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

def emit_umin(tmp_id, args):
    c("uint64_t tmp" + str(tmp_id) +" = MIN(" + args[1] + ", " + args[0] + ");")
    return tmp_id + 1

ops = {}
#             (n operands, emitter1, emitter2)
ops["FADD"] = (2, emit_fadd, mathml_splice_add)
ops["FDIV"] = (2, emit_fdiv, mathml_splice_div)
ops["FMAX"] = (2, emit_fmax, mathml_splice_max)
ops["FMUL"] = (2, emit_fmul, mathml_splice_mul)
ops["FSUB"] = (2, emit_fsub, mathml_splice_sub)
ops["READ"] = (2, emit_read, mathml_splice_read)
ops["UADD"] = (2, emit_uadd, mathml_splice_add)
ops["UDIV"] = (2, emit_udiv, mathml_splice_div)
ops["UMUL"] = (2, emit_umul, mathml_splice_mul)
ops["USUB"] = (2, emit_usub, mathml_splice_sub)
ops["UMIN"] = (2, emit_umin, mathml_splice_min)

def brkt(subexp):
    if " " in subexp:
        return "(" + subexp + ")"
    else:
        return subexp

def splice_bitwise_and(args):
    return brkt(args[1]) + " & " + brkt(args[0])

def splice_logical_and(args):
    return brkt(args[1]) + " && " + brkt(args[0])

def splice_ult(args):
    return brkt(args[1]) + " < " + brkt(args[0])

def splice_ugte(args):
    return brkt(args[1]) + " >= " + brkt(args[0])

exp_ops = {}
#                 (n operands, splicer)
exp_ops["AND"]  = (2, splice_bitwise_and)
exp_ops["UGTE"] = (2, splice_ugte)
exp_ops["ULT"]  = (2, splice_ult)
exp_ops["&&"]   = (2, splice_logical_and)


hw_vars = {}
hw_vars["$EuCoresTotalCount"] = "devinfo->n_eus"
hw_vars["$EuSlicesTotalCount"] = "devinfo->n_eu_slices"
hw_vars["$EuSubslicesTotalCount"] = "devinfo->n_eu_sub_slices"
hw_vars["$EuThreadsCount"] = "devinfo->eu_threads_count"
hw_vars["$SliceMask"] = "devinfo->slice_mask"
hw_vars["$SubsliceMask"] = "devinfo->subslice_mask"
hw_vars["$GpuTimestampFrequency"] = "devinfo->timestamp_frequency"
hw_vars["$GpuMinFrequencyMHz"] = "devinfo->gt_min_freq"
hw_vars["$GpuMaxFrequencyMHz"] = "devinfo->gt_max_freq"

counter_vars = {}

def splice_mathml_expression(equation, tag):
    tokens = equation.split()
    mathml_stack = []
    tmp_xml_operand = ""
    for token in tokens:
        if not mathml_stack:
            token = check_operand_type(token)
        mathml_stack.append([token, default_precedence])
        while mathml_stack and mathml_stack[-1][0] in ops:
            op = mathml_stack.pop()[0]
            argc, callback, mathml_callback = ops[op]
            xml_args = []
            for i in range(0, argc):
                xml_operand = mathml_stack.pop()
                xml_args.append(xml_operand)
            tmp_xml_operand = mathml_callback(xml_args)
            mathml_stack.append(tmp_xml_operand)
    xml_string = mathml_stack.pop()[0]
    equation_descr = "<mi>" + tag + "</mi><mo> = </mo>"
    return "<mathml_" + tag + ">" + equation_descr + xml_string + "</mathml_" + tag + ">"

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
            argc, callback, mathml_callback = ops[op]
            args = []
            for i in range(0, argc):
                operand = stack.pop()
                if operand[0] == "$":
                    if operand in hw_vars:
                        operand = hw_vars[operand]
                    elif operand in counter_vars:
                        reference = counter_vars[operand]
                        operand = read_funcs[operand[1:]] + "(devinfo, metric_set, accumulator)"
                    else:
                        raise Exception("Failed to resolve variable " + operand + " in equation " + equation + " for " + set.get('name') + " :: " + counter.get('name'));
                args.append(operand)

            tmp_id = callback(tmp_id, args)
            tmp = "tmp" + str(tmp_id - 1)
            stack.append(tmp)

    if len(stack) != 1:
        raise Exception("Spurious empty rpn code for " + set.get('name') + " :: " +
                counter.get('name') + ".\nThis is probably due to some unhandled RPN function, in the equation \"" +
                equation + "\"")

    value = stack.pop()

    if value in hw_vars:
        value = hw_vars[value]

    c("\nreturn " + value + ";")

def splice_rpn_expression(set, counter, expression):
    tokens = expression.split()
    stack = []

    for token in tokens:
        stack.append(token)
        while stack and stack[-1] in exp_ops:
            op = stack.pop()
            argc, callback = exp_ops[op]
            args = []
            for i in range(0, argc):
                operand = stack.pop()
                if operand[0] == "$":
                    if operand in hw_vars:
                        operand = hw_vars[operand]
                    else:
                        raise Exception("Failed to resolve variable " + operand + " in expression " + expression + " for " + set.get('name') + " :: " + counter.get('name'));
                args.append(operand)

            subexp = callback(args)

            stack.append(subexp)

    if len(stack) != 1:
        raise Exception("Spurious empty rpn expression for " + set.get('name') + " :: " +
                counter.get('name') + ".\nThis is probably due to some unhandled RPN operation, in the expression \"" +
                expression + "\"")

    return stack.pop()

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
    c("const struct gputop_metric_set *metric_set,\n")
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
    c("static uint64_t")
    max_sym = set.get('chipset').lower() + "__" + set.get('underscore_name') + "__" + counter.get('underscore_name') + "__max"
    c(max_sym + "(struct gputop_devinfo *devinfo,\n")
    c_indent(len(max_sym) + 1)
    c("const struct gputop_metric_set *metric_set,\n")
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

    c("\n")

    availability = counter.get('availability')
    if availability:
        expression = splice_rpn_expression(set, counter, availability)
        lines = expression.split(' && ')
        n_lines = len(lines)
        if n_lines == 1:
            c("if (" + lines[0] + ") {")
        else:
            c("if (" + lines[0] + " &&")
            c_indent(4)
            for i in range(1, (n_lines - 1)):
                c(lines[i] + " &&")
            c(lines[(n_lines - 1)] + ") {")
            c_outdent(4)
        c_indent(4)

    c("counter = &metric_set->counters[metric_set->n_counters++];\n")
    c("counter->oa_counter_read_" + data_type + " = " + read_funcs[counter.get('symbol_name')] + ";\n")
    c("counter->name = \"" + counter.get('name') + "\";\n")
    c("counter->symbol_name = \"" + counter.get('symbol_name') + "\";\n")
    c("counter->desc = \"" + counter.get('description') + "\";\n")
    c("counter->type = GPUTOP_PERFQUERY_COUNTER_" + semantic_type_uc + ";\n")
    c("counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_" + data_type_uc + ";\n")
    c("counter->max = " + max_funcs[counter.get('symbol_name')] + "\n")

    if availability:
        c_outdent(4)
        c("}\n")


parser = argparse.ArgumentParser()
parser.add_argument("xml", help="XML description of metrics")
parser.add_argument("--header", help="Header file to write")
parser.add_argument("--code", help="C file to write")
parser.add_argument("--chipset", help="Chipset to generate code for")
parser.add_argument("--xml-out", help="Output XML filename")

args = parser.parse_args()

chipset = args.chipset.lower()

if args.header:
    header_file = open(args.header, 'w')

if args.code:
    c_file = open(args.code, 'w')

tree = ET.parse(args.xml)
if args.xml_out:
    open(args.xml_out, 'w')


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

#include "gputop-oa-metrics.h"

""")

c(copyright)
c(
"""
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

""")

c("#include \"oa-" + chipset + ".h\"")

c(
"""
#include <stdlib.h>
#include <string.h>

#include "gputop-oa-metrics.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

extern void gputop_register_oa_metric_set(struct gputop_metric_set *metric_set);

static inline void *
xmalloc0(size_t size)
{
    void *ret = malloc(size);
    if (!ret)
        exit(1);
    memset(ret, 0, size);
    return ret;
}

static uint64_t
percentage_max_callback(struct gputop_devinfo *devinfo,
                        const struct gputop_metric_set *metric_set,
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

    assert set.get('chipset').lower() == chipset

    for counter in counters:
        empty_vars = {}
        read_funcs[counter.get('symbol_name')] = output_counter_read(set, counter, counter_vars)
        max_funcs[counter.get('symbol_name')] = output_counter_max(set, counter, counter_vars)
        counter_vars["$" + counter.get('symbol_name')] = counter
        xml_equation = splice_mathml_expression(counter.get('equation'), "EQ")
        counter.append(ET.fromstring(xml_equation))
        if counter.get('max_equation'):
            xml_max_equation = splice_mathml_expression(counter.get('max_equation'), "MAX_EQ")
            counter.append(ET.fromstring(xml_max_equation))

    c("\nstatic void\n")
    c("add_" + set.get('underscore_name') + "_metric_set(struct gputop_devinfo *devinfo)\n")
    c("{\n")
    c_indent(3)

    c("struct gputop_metric_set *metric_set;\n")
    c("struct gputop_metric_set_counter *counter;\n\n")

    c("metric_set = xmalloc0(sizeof(struct gputop_metric_set));\n")
    c("metric_set->name = \"" + set.get('name') + "\";\n")
    c("metric_set->symbol_name = \"" + set.get('symbol_name') + "\";\n")
    c("metric_set->guid = \"" + set.get('guid') + "\";\n")
    c("metric_set->counters = xmalloc0(sizeof(struct gputop_metric_set_counter) * " + str(len(counters)) + ");\n")
    c("metric_set->n_counters = 0;\n")
    c("metric_set->perf_oa_metrics_set = 0; // determined at runtime\n")

    if chipset == "hsw":
        c("""metric_set->perf_oa_format = I915_OA_FORMAT_A45_B8_C8;

metric_set->perf_raw_size = 256;
metric_set->gpu_time_offset = 0;
metric_set->a_offset = 1;
metric_set->b_offset = metric_set->a_offset + 45;
metric_set->c_offset = metric_set->b_offset + 8;

""")
    else:
        c("""metric_set->perf_oa_format = I915_OA_FORMAT_A32u40_A4u32_B8_C8;

metric_set->perf_raw_size = 256;
metric_set->gpu_time_offset = 0;
metric_set->gpu_clock_offset = 1;
metric_set->a_offset = 2;
metric_set->b_offset = metric_set->a_offset + 36;
metric_set->c_offset = metric_set->b_offset + 8;

""")

    for counter in counters:
        output_counter_report(set, counter)

    c("\n\ngputop_register_oa_metric_set(metric_set);\n")

    c_outdent(3)
    c("}\n")

if args.xml_out:
    tree.write(args.xml_out)

h("void gputop_oa_add_metrics_" + chipset + "(struct gputop_devinfo *devinfo);\n")

c("\nvoid")
c("gputop_oa_add_metrics_" + chipset + "(struct gputop_devinfo *devinfo)")
c("{")
c_indent(4)

for set in tree.findall(".//set"):
    c("add_" + set.get('underscore_name') + "_metric_set(devinfo);")

c_outdent(4)
c("}")
