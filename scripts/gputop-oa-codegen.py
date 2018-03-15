#!/usr/bin/env python2
#
# Copyright (c) 2015-2018 Intel Corporation
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

import argparse
import os
import sys
import textwrap

import xml.etree.cElementTree as et

import pylibs.codegen as codegen

h = None
c = None

hashed_funcs = {}
xml_equations = None

def check_operand_type(arg):
    if arg.isdigit():
        return "\n<mn>" + arg + "</mn>"
    elif arg[0] == "$":
        if arg in counter_vars:
            description = counter_vars[arg].get('description')
        elif arg in hw_vars and 'desc' in hw_vars[arg]:
            description = hw_vars[arg]['desc'];
        else:
            description = None

        if description != None:
            return "\n<maction actiontype='tooltip'>\n<mi>" + arg + "</mi>\n<mtext>" + description + "</mtext>\n</maction>"
        else:
            return "<mi>" + arg + "</mi>"
    return arg

# http://en.cppreference.com/w/c/language/operator_precedence
and_precedence = 8
shft_precedence = 5
mul_precedence = 3
add_precedence = 2
sub_precedence = 2
default_precedence = 16 #a high value which denotes no brackets needed

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

def mathml_splice_lshft(args):
    operand_0 = check_operand_type(args[0][0])
    operand_1 = check_operand_type(args[1][0])
    if args[0][1] < shft_precedence:
        operand_0 = put_brackets(args[0][0])
    if args[1][1] < shft_precedence:
        operand_1 = put_brackets(args[1][0])
    return [operand_1 + "\n<mo>&lt;&lt;</mo>" + operand_0, shft_precedence]

def mathml_splice_rshft(args):
    operand_0 = check_operand_type(args[0][0])
    operand_1 = check_operand_type(args[1][0])
    if args[0][1] < mul_precedence:
        operand_0 = put_brackets(args[0][0])
    if args[1][1] < mul_precedence:
        operand_1 = put_brackets(args[1][0])
    return [operand_1 + "\n<mo>&gt;&gt;</mo>" + operand_0, mul_precedence]

def mathml_splice_and(args):
    operand_0 = check_operand_type(args[0][0])
    operand_1 = check_operand_type(args[1][0])
    if args[0][1] < and_precedence:
        operand_0 = put_brackets(args[0][0])
    if args[1][1] < and_precedence:
        operand_1 = put_brackets(args[1][0])
    return [operand_1 + "\n<mo>&amp;</mo>" + operand_0, and_precedence]

def emit_fadd(tmp_id, args):
    c("double tmp{0} = {1} + {2};".format(tmp_id, args[1], args[0]))
    return tmp_id + 1

# Be careful to check for divide by zero...
def emit_fdiv(tmp_id, args):
    c("double tmp{0} = {1};".format(tmp_id, args[1]))
    c("double tmp{0} = {1};".format(tmp_id + 1, args[0]))
    c("double tmp{0} = tmp{1} ? tmp{2} / tmp{1} : 0;".format(tmp_id + 2, tmp_id + 1, tmp_id))
    return tmp_id + 3

def emit_fmax(tmp_id, args):
    c("double tmp{0} = {1};".format(tmp_id, args[1]))
    c("double tmp{0} = {1};".format(tmp_id + 1, args[0]))
    c("double tmp{0} = MAX(tmp{1}, tmp{2});".format(tmp_id + 2, tmp_id, tmp_id + 1))
    return tmp_id + 3

def emit_fmul(tmp_id, args):
    c("double tmp{0} = {1} * {2};".format(tmp_id, args[1], args[0]))
    return tmp_id + 1

def emit_fsub(tmp_id, args):
    c("double tmp{0} = {1} - {2};".format(tmp_id, args[1], args[0]))
    return tmp_id + 1

def emit_read(tmp_id, args):
    type = args[1].lower()
    c("uint64_t tmp{0} = accumulator[metric_set->{1}_offset + {2}];".format(tmp_id, type, args[0]))
    return tmp_id + 1

def emit_uadd(tmp_id, args):
    c("uint64_t tmp{0} = {1} + {2};".format(tmp_id, args[1], args[0]))
    return tmp_id + 1

# Be careful to check for divide by zero...
def emit_udiv(tmp_id, args):
    c("uint64_t tmp{0} = {1};".format(tmp_id, args[1]))
    c("uint64_t tmp{0} = {1};".format(tmp_id + 1, args[0]))
    c("uint64_t tmp{0} = tmp{1} ? tmp{2} / tmp{1} : 0;".format(tmp_id + 2, tmp_id + 1, tmp_id))
    return tmp_id + 3

def emit_umul(tmp_id, args):
    c("uint64_t tmp{0} = {1} * {2};".format(tmp_id, args[1], args[0]))
    return tmp_id + 1

def emit_usub(tmp_id, args):
    c("uint64_t tmp{0} = {1} - {2};".format(tmp_id, args[1], args[0]))
    return tmp_id + 1

def emit_umin(tmp_id, args):
    c("uint64_t tmp{0} = MIN({1}, {2});".format(tmp_id, args[1], args[0]))
    return tmp_id + 1

def emit_lshft(tmp_id, args):
    c("uint64_t tmp{0} = {1} << {2};".format(tmp_id, args[1], args[0]))
    return tmp_id + 1

def emit_rshft(tmp_id, args):
    c("uint64_t tmp{0} = {1} >> {2};".format(tmp_id, args[1], args[0]))
    return tmp_id + 1

def emit_and(tmp_id, args):
    c("uint64_t tmp{0} = {1} & {2};".format(tmp_id, args[1], args[0]))
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
ops["<<"]   = (2, emit_lshft, mathml_splice_lshft)
ops[">>"]   = (2, emit_rshft, mathml_splice_rshft)
ops["AND"]  = (2, emit_and, mathml_splice_and)

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


hw_vars = {
        "$EuCoresTotalCount": { 'c': "devinfo->n_eus", 'desc': "The total number of execution units" },
        "$EuSlicesTotalCount": { 'c': "devinfo->n_eu_slices" },
        "$EuSubslicesTotalCount": { 'c': "devinfo->n_eu_sub_slices" },
        "$EuThreadsCount": { 'c': "devinfo->eu_threads_count" },
        "$SliceMask": { 'c': "devinfo->slice_mask" },
        "$SubsliceMask": { 'c': "devinfo->subslice_mask" },
        "$GpuTimestampFrequency": { 'c': "devinfo->timestamp_frequency" },
        "$GpuMinFrequency": { 'c': "devinfo->gt_min_freq" },
        "$GpuMaxFrequency": { 'c': "devinfo->gt_max_freq" },
        "$SkuRevisionId": { 'c': "devinfo->revision" },
}

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

def output_rpn_equation_code(set, counter, equation):
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
                        operand = hw_vars[operand]['c']
                    elif operand in set.counter_vars:
                        reference = set.counter_vars[operand]
                        operand = set.read_funcs[operand[1:]] + "(devinfo, metric_set, accumulator)"
                    else:
                        raise Exception("Failed to resolve variable " + operand + " in equation " + equation + " for " + set.name + " :: " + counter.get('name'));
                args.append(operand)

            tmp_id = callback(tmp_id, args)

            tmp = "tmp{0}".format(tmp_id - 1)
            stack.append(tmp)

    if len(stack) != 1:
        raise Exception("Spurious empty rpn code for " + set.name + " :: " +
                counter.get('name') + ".\nThis is probably due to some unhandled RPN function, in the equation \"" +
                equation + "\"")

    value = stack[-1]

    if value in hw_vars:
        value = hw_vars[value]['c']
    if value in set.counter_vars:
        value = set.read_funcs[value[1:]] + "(devinfo, metric_set, accumulator)"

    c("\nreturn " + value + ";")

def splice_rpn_expression(set, counter_name, expression):
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
                        operand = hw_vars[operand]['c']
                    else:
                        raise Exception("Failed to resolve variable " + operand + " in expression " + expression + " for " + set.name + " :: " + counter_name)
                args.append(operand)

            subexp = callback(args)

            stack.append(subexp)

    if len(stack) != 1:
        raise Exception("Spurious empty rpn expression for " + set.name + " :: " +
                counter_name + ".\nThis is probably due to some unhandled RPN operation, in the expression \"" +
                expression + "\"")

    return stack[-1]


def data_type_to_ctype(ret_type):
    if ret_type == "uint64":
        return "uint64_t"
    elif ret_type == "float":
        return "double"
    else:
        raise Exception("Unhandled case for mapping \"" + ret_type + "\" to a C type")


def output_counter_read(gen, set, counter):
    read_eq = counter.get('equation')

    c("\n")
    c("/* {0} :: {1} */".format(set.name, counter.get('name')))
    ret_type = counter.get('data_type')
    ret_ctype = data_type_to_ctype(ret_type)
    read_sym = gen.counter_read_sym(set, counter)

    if read_eq in hashed_funcs:
        c("#define %s \\" % read_sym)
        c.indent(4)
        c("%s" % hashed_funcs[read_eq])
        c.outdent(4)
    else:
        c("static " + ret_ctype)
        c(read_sym + "(const struct gputop_devinfo *devinfo,\n")
        c.indent(len(read_sym) + 1)
        c("const struct gputop_metric_set *metric_set,\n")
        c("uint64_t *accumulator)\n")
        c.outdent(len(read_sym) + 1)

        c("{")
        c.indent(4)

        output_rpn_equation_code(set, counter, read_eq)

        c.outdent(4)
        c("}")

        hashed_funcs[read_eq] = read_sym


def output_counter_max(gen, set, counter):
    max_eq = counter.get('max_equation')

    if not max_eq or max_eq == "100":
        return

    ret_type = counter.get('data_type')
    ret_ctype = data_type_to_ctype(ret_type)
    max_sym = gen.counter_max_sym(set, counter)

    c("\n")
    c("/* {0} :: {1} */".format(set.name, counter.get('name')))

    if max_eq in hashed_funcs:
        c("#define %s \\" % max_sym)
        c.indent(4)
        c("%s" % hashed_funcs[max_eq])
        c.outdent(4)
    else:
        c("static " + ret_ctype)

        c(max_sym + "(const struct gputop_devinfo *devinfo,\n")
        c.indent(len(max_sym) + 1)
        c("const struct gputop_metric_set *metric_set,\n")
        c("uint64_t *accumulator)\n")
        c.outdent(len(max_sym) + 1)

        c("{")
        c.indent(4)

        output_rpn_equation_code(set, counter, max_eq)

        c.outdent(4)
        c("}")

        hashed_funcs[max_eq] = max_sym


semantic_type_map = {
    "duration": "raw",
    "ratio": "event"
    }

def output_availability(set, availability, counter_name):
    expression = splice_rpn_expression(set, counter_name, availability)
    lines = expression.split(' && ')
    n_lines = len(lines)
    if n_lines == 1:
        c("if (" + lines[0] + ") {")
    else:
        c("if (" + lines[0] + " &&")
        c.indent(4)
        for i in range(1, (n_lines - 1)):
            c(lines[i] + " &&")
        c(lines[(n_lines - 1)] + ") {")
        c.outdent(4)


def output_units(unit):
    return unit.replace(' ', '_').upper()


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
        output_availability(set, availability, counter.get('name'))
        c.indent(4)

    c("counter = &metric_set->counters[metric_set->n_counters++];\n")
    c("counter->metric_set = metric_set;\n")
    c("counter->oa_counter_read_" + data_type + " = " + set.read_funcs[counter.get('symbol_name')] + ";\n")
    c("counter->name = \"" + counter.get('name') + "\";\n")
    c("counter->symbol_name = \"" + counter.get('symbol_name') + "\";\n")
    c("counter->desc = \"" + counter.get('description') + "\";\n")
    c("counter->type = GPUTOP_PERFQUERY_COUNTER_" + semantic_type_uc + ";\n")
    c("counter->data_type = GPUTOP_PERFQUERY_COUNTER_DATA_" + data_type_uc + ";\n")
    c("counter->units = GPUTOP_PERFQUERY_COUNTER_UNITS_" + output_units(counter.get('units')) + ";\n")
    c("counter->max_" + data_type + " = " + set.max_funcs[counter.get('symbol_name')] + ";\n")

    if availability:
        c.outdent(4)
        c("}\n")


def generate_register_configs(set):
    register_types = {
        'FLEX': 'flex_regs',
        'NOA': 'mux_regs',
        'OA': 'b_counter_regs',
    }

    # allocate memory
    total_n_registers = {}
    register_configs = set.findall('register_config')
    for register_config in register_configs:
        t = register_types[register_config.get('type')]
        if t not in total_n_registers:
            total_n_registers[t] = len(register_config.findall('register'))
        else:
            total_n_registers[t] += len(register_config.findall('register'))

    for reg in total_n_registers:
        c("metric_set->%s = xmalloc0(sizeof(*metric_set->%s) * %i);" %
          (reg, reg, total_n_registers[reg]))
    c("\n")

    # fill in register/values
    register_configs = set.findall('register_config')
    for register_config in register_configs:
        t = register_types[register_config.get('type')]

        availability = register_config.get('availability')
        if availability:
            output_availability(set, availability, register_config.get('type') + ' register config')
            c.indent(4)

        for register in register_config.findall('register'):
            c("metric_set->%s[metric_set->n_%s++] = (struct gputop_register_prog) { .reg = %s, .val = %s };" %
              (t, t, register.get('address'), register.get('value')))

        if availability:
            c.outdent(4)
            c("}")
        c("\n")

    for t in total_n_registers:
        c("assert(metric_set->n_{0} <= {1});".format(t, total_n_registers[t]))

#
class Set:
    def __init__(self, gen, xml):
        self.gen = gen
        self.xml = xml

        self.counter_vars = {}
        self.max_funcs = {}
        self.read_funcs = {}

        counters = self.xml.findall("counter")
        for counter in counters:
            self.counter_vars["$" + counter.get('symbol_name')] = counter
            self.max_funcs[counter.get('symbol_name')] = self.gen.counter_max_sym(self, counter)
            self.read_funcs[counter.get('symbol_name')] = self.gen.counter_read_sym(self, counter)

    @property
    def hw_config_guid(self):
        return self.xml.get('hw_config_guid')

    @property
    def name(self):
        return self.xml.get('name')

    @property
    def symbol_name(self):
        return self.xml.get('symbol_name')

    @property
    def underscore_name(self):
        return self.xml.get('underscore_name')

    def findall(self, path):
        return self.xml.findall(path)

    def find(self, path):
        return self.xml.find(path)


class Gen:
    def __init__(self, filename):
        self.filename = filename
        self.xml = et.parse(self.filename)
        self.chipset = self.xml.find('.//set').get('chipset').lower()
        self.sets = []

        for xml_set in self.xml.findall(".//set"):
            self.sets.append(Set(self, xml_set))

    def counter_read_sym(self, set, counter):
        return "{0}__{1}__{2}__read".format(self.chipset, set.underscore_name, counter.get('underscore_name'))

    def counter_max_sym(self, set, counter):
        max_eq = counter.get('max_equation')
        if not max_eq:
            return "NULL /* undefined */"
        if max_eq == "100":
            return "percentage_max_callback_" + counter.get('data_type')
        return "{0}__{1}__{2}__max".format(self.chipset, set.underscore_name, counter.get('underscore_name'))


def main():
    global c
    global h
    global xml_equations

    parser = argparse.ArgumentParser()
    parser.add_argument("--header", help="Header file to write")
    parser.add_argument("--code", help="C file to write")
    parser.add_argument("--xml-out", help="Output XML files (adding mathml equations)")
    parser.add_argument("xml_files", nargs='+', help="List of xml metrics files to process")

    args = parser.parse_args()

    # Note: either arg may == None
    h = codegen.Codegen(args.header)
    c = codegen.Codegen(args.code)

    gens = []
    for xml_file in args.xml_files:
        gens.append(Gen(xml_file))

    if args.xml_out:
        for gen in gens:
            for set in gen.sets:
                counters = set.findall('counter')
                for counter in counters:
                    xml_equation = splice_mathml_expression(counter.get('equation'), "EQ")
                    counter.append(et.fromstring(xml_equation))
            gen.xml.write(gen.filename)


    copyright = textwrap.dedent("""\
        /* Autogenerated file, DO NOT EDIT manually! generated by {}
         *
         * Copyright (c) 2018 Intel Corporation
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

        """).format(os.path.basename(__file__))

    h(copyright)
    c(copyright)
    c(textwrap.dedent("""\
        #include <stddef.h>
        #include <stdint.h>
        #include <stdbool.h>
        #include <assert.h>

        """))

    c("#include \"" + os.path.basename(args.header) + "\"")

    c(textwrap.dedent("""\
        #include <stdlib.h>
        #include <string.h>

        #include "gputop-oa-metrics.h"

        #define MIN(x, y) (((x) < (y)) ? (x) : (y))
        #define MAX(a, b) (((a) > (b)) ? (a) : (b))

        static inline void *
        xmalloc0(size_t size)
        {
            void *ret = malloc(size);
            if (!ret)
                exit(1);
            memset(ret, 0, size);
            return ret;
        }

        static double
        percentage_max_callback_float(const struct gputop_devinfo *devinfo,
                                      const struct gputop_metric_set *metric_set,
                                      uint64_t *accumulator)
        {
           return 100;
        }

        static uint64_t
        percentage_max_callback_uint64(const struct gputop_devinfo *devinfo,
                                       const struct gputop_metric_set *metric_set,
                                       uint64_t *accumulator)
        {
           return 100;
        }

        """))

    # Print out all equation functions.
    for gen in gens:
        for set in gen.sets:
            counters = set.findall("counter")
            for counter in counters:
                output_counter_read(gen, set, counter)
                output_counter_max(gen, set, counter)

    # Print out all set registration functions for each set in each
    # generation.
    for gen in gens:
        for set in gen.sets:
            c("\nstatic void\n")
            c(gen.chipset + "_add_" + set.underscore_name + "_metric_set(const struct gputop_devinfo *devinfo,\n" +
              "    void (*register_metric_set)(const struct gputop_metric_set *, void *), void *data)\n")
            c("{\n")
            c.indent(4)

            c("struct gputop_metric_set *metric_set;\n")
            c("struct gputop_metric_set_counter *counter;\n\n")

            counters = sorted(set.findall("counter"), key=lambda k: k.get('symbol_name'))

            c("metric_set = xmalloc0(sizeof(struct gputop_metric_set));\n")
            c("metric_set->name = \"" + set.name + "\";\n")
            c("metric_set->symbol_name = \"" + set.symbol_name + "\";\n")
            c("metric_set->hw_config_guid = \"" + set.hw_config_guid + "\";\n")
            c("metric_set->counters = xmalloc0(sizeof(struct gputop_metric_set_counter) * {0});\n".format(str(len(counters))))
            c("metric_set->n_counters = 0;\n")
            c("metric_set->perf_oa_metrics_set = 0; // determined at runtime\n")

            if gen.chipset == "hsw":
                c(textwrap.dedent("""\
                    metric_set->perf_oa_format = I915_OA_FORMAT_A45_B8_C8;

                    metric_set->perf_raw_size = 256;
                    metric_set->gpu_time_offset = 0;
                    metric_set->a_offset = 1;
                    metric_set->b_offset = metric_set->a_offset + 45;
                    metric_set->c_offset = metric_set->b_offset + 8;

                    """))
            else:
                c(textwrap.dedent("""\
                    metric_set->perf_oa_format = I915_OA_FORMAT_A32u40_A4u32_B8_C8;

                    metric_set->perf_raw_size = 256;
                    metric_set->gpu_time_offset = 0;
                    metric_set->gpu_clock_offset = 1;
                    metric_set->a_offset = 2;
                    metric_set->b_offset = metric_set->a_offset + 36;
                    metric_set->c_offset = metric_set->b_offset + 8;

                    """))

            generate_register_configs(set)

            for counter in counters:
                output_counter_report(set, counter)

            c("\nassert(metric_set->n_counters <= {0});\n".format(len(counters)));

            c("\nregister_metric_set(metric_set, data);\n")

            c.outdent(4)
            c("}\n")

    h(textwrap.dedent("""\
        #pragma once

        #include "gputop-oa-metrics.h"

        #ifdef __cplusplus
        extern "C" {
        #endif

        """))

    # Print out all set registration functions for each generation.
    for gen in gens:
        h("void gputop_oa_add_metrics_" + gen.chipset + "(const struct gputop_devinfo *devinfo,\n"
          "    void (*register_metric_set)(const struct gputop_metric_set *, void *), void *data);\n\n")

        c("\nvoid")
        c("gputop_oa_add_metrics_" + gen.chipset + "(const struct gputop_devinfo *devinfo,\n"
          "    void (*register_metric_set)(const struct gputop_metric_set *, void *), void *data)")
        c("{")
        c.indent(4)

        for set in gen.sets:
            c("{0}_add_{1}_metric_set(devinfo, register_metric_set, data);".format(gen.chipset, set.underscore_name))

        c.outdent(4)
        c("}")

    h(textwrap.dedent("""\
        #ifdef __cplusplus
        } /* extern C */
        #endif

        """))


if __name__ == '__main__':
    main()
