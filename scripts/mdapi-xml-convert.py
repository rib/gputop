#!/usr/bin/env python2

# Copyright (C) 2015-2016 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


# "MDAPI" xml files are an XML schema for maintaining meta data about Gen
# graphics Ovservability counters, where MD API is the name of a library shared
# by Intel GPA and Intel VTune.
#
# These files aren't publicly documented and have some historical baggage that
# adds some complexity as well as being inconsistent in a number of ways that
# makes it quite a bit of effort to parse/use the data. We also don't have
# guarantees about how this schema is maintained.
#
# We've taken the opportunity to find ways to simplify the input data and to
# make it more consistent to hopefully reduce the effort involved in using the
# data downstream.
#


import argparse
import copy
import hashlib
from operator import itemgetter
import re
import sys
import time
import uuid

import xml.etree.ElementTree as et
import xml.sax.saxutils as saxutils

import pylibs.oa_guid_registry as oa_registry


# MDAPI configs include writes to some non-config registers,
# thus the blacklists...

gen8_11_chipset_params = {
    'a_offset': 16,
    'b_offset': 192,
    'c_offset': 224,
    'oa_report_size': 256,
    'config_reg_blacklist': {
        0x2364, # OACTXID
    },
}

chipsets = {
    'HSW': {
        'a_offset': 12,
        'b_offset': 192,
        'c_offset': 224,
        'oa_report_size': 256,
        'registers': {
            # TODO extend the symbol table for nicer output...
                0x2710: { 'name': 'OASTARTTRIG1' },
            0x2714: { 'name': 'OASTARTTRIG1' },
            0x2718: { 'name': 'OASTARTTRIG1' },
            0x271c: { 'name': 'OASTARTTRIG1' },
            0x2720: { 'name': 'OASTARTTRIG1' },
            0x2724: { 'name': 'OASTARTTRIG6' },
            0x2728: { 'name': 'OASTARTTRIG7' },
            0x272c: { 'name': 'OASTARTTRIG8' },
            0x2740: { 'name': 'OAREPORTTRIG1' },
            0x2744: { 'name': 'OAREPORTTRIG2' },
            0x2748: { 'name': 'OAREPORTTRIG3' },
            0x274c: { 'name': 'OAREPORTTRIG4' },
            0x2750: { 'name': 'OAREPORTTRIG5' },
            0x2754: { 'name': 'OAREPORTTRIG6' },
            0x2758: { 'name': 'OAREPORTTRIG7' },
            0x275c: { 'name': 'OAREPORTTRIG8' },
            0x2770: { 'name': 'OACEC0_0' },
            0x2774: { 'name': 'OACEC0_1' },
            0x2778: { 'name': 'OACEC1_0' },
            0x277c: { 'name': 'OACEC1_1' },
            0x2780: { 'name': 'OACEC2_0' },
            0x2784: { 'name': 'OACEC2_1' },
            0x2788: { 'name': 'OACEC3_0' },
            0x278c: { 'name': 'OACEC3_1' },
            0x2790: { 'name': 'OACEC4_0' },
            0x2794: { 'name': 'OACEC4_1' },
            0x2798: { 'name': 'OACEC5_0' },
            0x279c: { 'name': 'OACEC5_1' },
            0x27a0: { 'name': 'OACEC6_0' },
            0x27a4: { 'name': 'OACEC6_1' },
            0x27a8: { 'name': 'OACEC7_0' },
            0x27ac: { 'name': 'OACEC7_1' },
        },
        'config_reg_blacklist': {
            0x2364, # OASTATUS1 register
        },
    },
    'BDW': gen8_11_chipset_params,
    'CHV': gen8_11_chipset_params,
    'SKLGT2': gen8_11_chipset_params,
    'SKLGT3': gen8_11_chipset_params,
    'SKLGT4': gen8_11_chipset_params,
    'BXT': gen8_11_chipset_params,
    'KBLGT2': gen8_11_chipset_params,
    'KBLGT3': gen8_11_chipset_params,
    'GLK': gen8_11_chipset_params,
    'CFLGT2': gen8_11_chipset_params,
    'CFLGT3': gen8_11_chipset_params,
    'CNL': gen8_11_chipset_params,
    'ICL': gen8_11_chipset_params,
    'LKF': gen8_11_chipset_params,
    'TGL': gen8_11_chipset_params,
}

register_types = { 'OA', 'NOA', 'FLEX', 'PM' }

default_set_blacklist = { "RenderDX1x", # TODO: rename to something non 'DX'
                                        # specific if this config is generally
                                        # usefull
                          "RenderBalance", # XXX: missing register config
                        }

counter_blacklist = {
    "DramLlcThroughput", # TODO: The max equation of this counter
                         # requires dram throughtput value. Need to
                         # investiguate how to get this value.
}

sys_vars = { "EuCoresTotalCount",
             "EuSlicesTotalCount",
             "SamplersTotalCount",
             "EuThreadsCount",
             "GpuMinFrequencyMHz",
             "GpuMaxFrequencyMHz",
             "GpuTimestampFrequency",
             "SliceMask",
             "SubsliceMask",
             "EuSubslicesTotalCount"
           }

def underscore(name):
    s = re.sub('MHz', 'Mhz', name)
    s = re.sub('\.', '_', s)
    s = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', s)
    return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s).lower()

def print_err(*args):
    sys.stderr.write(' '.join(map(str,args)) + '\n')

read_register_offsets = {
    0x1f0: 'PERFCNT1',
    0x1f8: 'PERFCNT2',
}

def read_value(chipset, offset):
    if offset in read_register_offsets:
        return read_register_offsets[offset]
    print_err("Unknown offset register at offset {0}".format(offset))
    assert 0


def read_token_to_rpn_read(chipset, token, raw_offsets):
    width, offset_str = token.split('@')

    # For Broadwell the raw read notation was extended for 40 bit
    # counters: rd40@<32bit_part1_offset>:<8bit_part2_offset>
    if width == "rd40":
        offset_32_str, offset_8_str = offset_str.split(':')
        offset_str = offset_32_str

    offset = int(offset_str, 16)

    if raw_offsets:
        a_offset = chipsets[chipset]['a_offset']
        b_offset = chipsets[chipset]['b_offset']
        c_offset = chipsets[chipset]['c_offset']
        report_size = chipsets[chipset]['oa_report_size']

        if offset < a_offset:
            if offset == 4:
                return "GPU_TIME 0 READ"
            elif offset == 12:
                assert chipset != "HSW" # Only for Gen8+
                return "GPU_CLOCK 0 READ"
            else:
                assert 0
        elif offset < b_offset:
            return "A " + str((offset - a_offset) / 4) + " READ"
        elif offset < c_offset:
            return "B " + str((offset - b_offset) / 4) + " READ"
        elif offset < report_size:
            return "C " + str((offset - c_offset) / 4) + " READ"
        else:
            return "{0} READ_REG".format(read_value(chipset, offset))
    else:
        idx = offset / 8
        if chipset == "HSW":
            # On Haswell accumulated counters are assumed to start
            # with GPU_TIME followed by 45 A counters, then 8 B
            # counters and finally 8 C counters.
            if idx < 1:
                return "GPU_TIME 0 READ"
            elif idx < 46:
                return "A " + str(idx - 1) + " READ"
            elif idx < 54:
                return "B " + str(idx - 46) + " READ"
            elif idx < 62:
                return "C " + str(idx - 54) + " READ"
            else:
                return "{0} READ_REG".format(read_value(chipset, offset))
        else:
            # For Gen8+ the array of accumulated counters is
            # assumed to start with a GPU_TIME then GPU_CLOCK,
            # then 36 A counters, then 8 B counters and finally
            # 8 C counters.
            if idx == 0:
                return "GPU_TIME 0 READ"
            elif idx == 1:
                return "GPU_CLOCK 0 READ"
            elif idx < 38:
                return "A " + str(idx - 2) + " READ"
            elif idx < 46:
                return "B " + str(idx - 38) + " READ"
            elif idx < 54:
                return "C " + str(idx - 46) + " READ"
            else:
                return "{0} READ_REG".format(read_value(chipset, offset))

    assert 0

def replace_read_tokens_with_rpn_read_ops(chipset, equation, raw_offsets):
    # MDAPI MetricSet equations use tokens like 'dw@0xff' for reading raw
    # values from snapshots, but this doesn't seem convenient for a few
    # reasons:
    #
    # 1) The offsets hide the particular a, b, or c counter they
    #    correspond to which in turn makes it awkward to experiment
    #    with different report sizes which trade off how many a, b and
    #    c counters are available
    #
    # 2) Raw reads could be represented as RPN operations too, and
    #    the consistency could make them slightly easier for tools to
    #    handle, E.g:
    #
    #      "A 5 READ" = read A counter 5
    #
    # We replace dw@ address tokens with GPU_TIME, A, B or C READ ops...
    #

    tokens = equation.split()
    equation = ""

    for token in tokens:
        if '@' in token:
            read_exp = read_token_to_rpn_read(chipset, token, raw_offsets)
            equation = equation + " " + read_exp
        else:
            equation = equation + " " + token

    return equation


parser = argparse.ArgumentParser()
parser.add_argument("xml", nargs="+", help="XML description of metrics")
parser.add_argument("--guids", required=True, help="Metric set GUID registry")
parser.add_argument("--whitelist", help="Only output for given, space-separated, sets")
parser.add_argument("--blacklist", help="Don't generate anything for given metric sets")
parser.add_argument("--merge", help="Additional meta data to merge into the result")
parser.add_argument("--dry-run", action="store_true",
                    help="Not generate new XML but to check any errors")

args = parser.parse_args()

metrics = et.Element('metrics')
tree = et.ElementTree(metrics)

def apply_aliases(text, aliases):
    if aliases == None:
        return text

    for alias in aliases.split(','):
        (a, b) = alias.split('|')
        text = re.sub(r"\b%s\b" % re.escape(a), b, text)

        a = a.lower()
        b = b.lower()
        text = re.sub(r"\b%s\b" % re.escape(a), b, text)

    return text

def strip_dx_apis(text):
    if text == None:
        return ""
    stripped = ""
    apis = text.split()
    for api in apis:
        if api[:2] != "DX":
            stripped = stripped + " " + api

    return stripped.strip()

# For recursively appending counters in order of dependencies...
def append_deps_and_counter(mdapi_counter, mdapi_counters, deps,
                            sorted_array, sorted_set):
    symbol_name = mdapi_counter.get('SymbolName')

    if symbol_name in sorted_set:
        return

    for dep_name in deps[symbol_name]:
        if dep_name in mdapi_counters:
            append_deps_and_counter(mdapi_counters[dep_name], mdapi_counters, deps,
                                    sorted_array, sorted_set)

    sorted_array.append(mdapi_counter)
    sorted_set[symbol_name] = mdapi_counter

def sort_counters(mdapi_counters, deps):
    sorted_array = []
    sorted_set = {} # counters in here have been added to array
    for symbol_name in mdapi_counters:
        append_deps_and_counter(mdapi_counters[symbol_name], mdapi_counters, deps,
                                sorted_array, sorted_set)

    return sorted_array

def expand_macros(equation):
    equation = equation.replace('GpuDuration', "$Self 100 UMUL $GpuCoreClocks FDIV")
    equation = equation.replace('EuAggrDuration', "$Self $EuCoresTotalCount UDIV 100 UMUL $GpuCoreClocks FDIV")
    return equation

def fixup_equation(equation):
    if equation is None:
        return None
    return equation.replace('$SubliceMask', '$SubsliceMask')

# The MDAPI XML files sometimes duplicate the same Flex EU/OA regs
# between configs with different AvailabilityEquations even though the
# availability checks are only expected to affect the MUX configs
#
# We iterate all the configs to filter out the FLEX/OA configs and
# double check that there's never any variations between repeated
# configs
#
def filter_single_config_registers_of_type(mdapi_metric_set, type):
    regs = []
    for mdapi_reg_config in mdapi_metric_set.findall("RegConfigStart"):
        tmp_regs = []
        for mdapi_reg in mdapi_reg_config.findall("Register"):
            reg = (int(mdapi_reg.get('offset'),16), int(mdapi_reg.get('value'),16))

            if reg[0] in chipsets[chipset]['config_reg_blacklist']:
                continue

            if mdapi_reg.get('type') == type:
                tmp_regs.append(reg)

        if len(tmp_regs) > 0:
            bad = False
            if len(regs) == 0:
                regs = tmp_regs
            elif len(regs) != len(tmp_regs):
                bad = True
            else:
                for i in xrange(0, len(regs)):
                    if regs[i] != tmp_regs[i]:
                        bad = True
                        break
            if bad:
                print_err("ERROR: multiple, differing FLEX/OA configs for one set: MetricSet=\"" + mdapi_metric_set.get('ShortName'))
                sys.exit(1)

    return regs


# We only have a very small number of IDs, but we aren't assuming they
# start from zero or are contiguous in the MDAPI XML files. Python
# doesn't seem to have a built in sparse array type so we just
# loop over the entries we have:
def get_mux_id_group(id_groups, id):
    for group in id_groups:
        if group['id'] == id:
            return group

    new_group = { 'id': id, 'configs': [] }
    id_groups.append(new_group)

    return new_group



def process_mux_configs(mdapi_set):
    allow_missing_id = True

    mux_config_id_groups = []

    for mdapi_reg_config in mdapi_set.findall("RegConfigStart"):

        mux_regs = []
        for mdapi_reg in mdapi_reg_config.findall("Register"):
            address = int(mdapi_reg.get('offset'), 16)

            if address in chipsets[chipset]['config_reg_blacklist']:
                continue

            reg_type = mdapi_reg.get('type')

            if reg_type not in register_types:
                print_err("ERROR: unknown register type=\"" + reg_type + "\": MetricSet=\"" + mdapi_set.get('ShortName'))
                sys.exit(1)

            if reg_type != 'NOA' and reg_type != 'PM':
                continue

            reg = (address, int(mdapi_reg.get('value'), 16))
            mux_regs.append(reg)

        if len(mux_regs) == 0:
            continue

        availability = mdapi_reg_config.get('AvailabilityEquation')
        if availability == "":
            availability = None

        if mdapi_reg_config.get('ConfigPriority') != None:
            reg_config_priority = int(mdapi_reg_config.get('ConfigPriority'))
        else:
            reg_config_priority = 0

        if mdapi_reg_config.get('ConfigId') != None:
            reg_config_id = int(mdapi_reg_config.get('ConfigId'))
            allow_missing_id = False
        elif mdapi_reg_config.get('ConfigId') == None and allow_missing_id == True:
            reg_config_id = 0
        else:
            # It will spell trouble if there's a mixture of explicit and
            # implied config IDs...
            print_err("ERROR: register configs mixing implied/explicit IDs: MetricSet=\"" + mdapi_set.get('ShortName'))
            sys.exit(1)

        mux_config = { 'priority': reg_config_priority,
                       'availability': availability,
                       'registers': mux_regs }

        mux_config_id_group = get_mux_id_group(mux_config_id_groups, reg_config_id)
        mux_config_id_group['configs'].append(mux_config)

    mux_config_id_groups.sort(key=itemgetter('id'))

    # The only special case we currently support for more than one group of NOA
    # MUX configs is for the Broadwell ComputeExtended metric set with two Id
    # groups and the second just has a single unconditional config that can
    # logically be appended to all the conditional configs of the first group
    if len(mux_config_id_groups) > 1:
        if len(mux_config_id_groups) != 2:
            print_err("ERROR: Script doesn't currently allow more than two groups of NOA MUX configs for a single metric set: MetricSet=\"" + mdapi_set.get('ShortName'))
            sys.exit(1)

        last_id_group = mux_config_id_groups[-1]
        if len(last_id_group['configs']) != 1:
            print_err("ERROR: Script currently only allows up to two Ids for NOA MUX configs if second Id only contains a single unconditional config: MetricSet=\"" + mdapi_set.get('ShortName'))
            sys.exit(1)

        tail_config = last_id_group['configs'][0]
        for mux_config in mux_config_id_groups[0]['configs']:
            mux_config['registers'] = mux_config['registers'] + tail_config['registers']

        mux_config_id_groups = [mux_config_id_groups[0]]

    if len(mux_config_id_groups) == 0 or mux_config_id_groups[0]['configs'] == 0:
        print_err("ERROR: MUX register configs missing: MetricSet=\"" + mdapi_set.get('ShortName'))
        sys.exit(1)

    mux_configs = mux_config_id_groups[0]['configs']
    assert isinstance(mux_configs, list)
    assert len(mux_configs) >= 1
    assert len(mux_configs[0]['registers']) > 1 # > 1 registers
    return mux_configs


def add_register_config(set, priority, availability, regs, type):
    reg_config = et.SubElement(set, 'register_config')

    reg_config.set('type', type)

    if availability != None:
        assert type == "NOA"
        reg_config.set('priority', str(priority))
        reg_config.set('availability', availability)

    for reg in regs:
        elem = et.SubElement(reg_config, 'register')
        elem.set('type', type)
        elem.set('address', "0x%08X" % reg[0])
        elem.set('value', "0x%08X" % reg[1])

def to_text(value):
    if value == None:
        return ""
    return value

# There are duplicated metric sets with the same symbol name so we
# keep track of the sets we've read so we can skip duplicates...
sets = {}

guids = {}

guids_xml = et.parse(args.guids)
for guid in guids_xml.findall(".//guid"):
    hashing_key = oa_registry.Registry.chipset_derive_hash(guid.get('chipset'),
                                                           guid.get('mdapi_config_hash'))
    guids[hashing_key] = guid.get('id')

for arg in args.xml:
    mdapi = et.parse(arg)

    concurrent_group = mdapi.find(".//ConcurrentGroup")

    for mdapi_set in mdapi.findall(".//MetricSet"):

        apis = mdapi_set.get('SupportedAPI')
        if "OGL" not in apis and "OCL" not in apis and "MEDIA" not in apis:
            continue

        set_symbol_name = mdapi_set.get('SymbolName')

        if set_symbol_name in sets:
            print_err("WARNING: duplicate set named \"" + set_symbol_name + "\" (SKIPPING)")
            continue

        chipset = mdapi_set.get('SupportedHW')
        if concurrent_group.get('SupportedGT') != None:
            chipset = chipset + concurrent_group.get('SupportedGT')
        if chipset not in chipsets:
            print_err("WARNING: unsupported chipset {0}, consider updating {1}".format(chipset, __file__))
            continue

        if args.whitelist:
            set_whitelist = args.whitelist.split()
            if set_symbol_name not in set_whitelist:
                continue

        if args.blacklist:
            set_blacklist = args.blacklist.split()
        else:
            set_blacklist = default_set_blacklist
        if set_symbol_name in set_blacklist:
            continue

        if mdapi_set.get('SnapshotReportSize') != '256':
            print_err("WARNING: skipping metric set '{0}', report size {1} invalid".format(set_symbol_name, mdapi_set.get('SnapshotReportSize')))
            continue

        set = et.SubElement(metrics, 'set')

        set.set('chipset', chipset)

        set.set('name', mdapi_set.get('ShortName'))
        set.set('symbol_name', set_symbol_name)
        set.set('underscore_name', underscore(mdapi_set.get('SymbolName')))
        set.set('mdapi_supported_apis', strip_dx_apis(mdapi_set.get('SupportedAPI')))


        # Look at the hardware register config before looking at the counters.
        #
        # The hardware configuration is used as a key to lookup up a GUID which
        # is used by applications to lookup the corresponding counter
        # normalization equations.
        #
        # We want to skip over any metric sets that don't yet have a registered
        # GUID in guids.xml.

        # There can be multiple NOA MUX configs, since they may have associated
        # availability tests to match particular systems.
        #
        # Unlike the MDAPI XML files we only support tracking one group of
        # mutually exclusive MUX configs, whereas the MDAPI XML files
        # theoretically allow a single metric set to be associated with ordered
        # groups of mutually exclusive configs. So far there is only one
        # Broadwell, ComputeExtended metric set which uses this, but that
        # particular case can be expressed in less general terms.
        #
        # Being a bit simpler here should make it easier for downstream tools
        # to deal with. (At least we got the handling of the Broadwell
        # ComputeExtended example wrong and it took several email exchanges and
        # a conference call to confirm how to interpret this case)
        mux_configs = process_mux_configs(mdapi_set)

        # Unlike for MUX registers, we only expect one set of FLEX/OA
        # registers per metric set (even though they are sometimes duplicated
        # between configs in MDAPI XML files.
        #
        # This filter function, extracts the register of a certain type but
        # also double checks that if they are repeated in separate configs that
        # they don't vary. (Notably the current i915 perf Linux driver would
        # need some adapting to support multiple OA/FLEX configs with different
        # availability expressions)
        #
        flex_regs = filter_single_config_registers_of_type(mdapi_set, "FLEX")
        oa_regs = filter_single_config_registers_of_type(mdapi_set, "OA")


        # Note: we ignore Perfmon registers

        for mux_config in mux_configs:
            add_register_config(set, mux_config['priority'], mux_config['availability'], mux_config['registers'], "NOA")
        if len(oa_regs) > 0:
            add_register_config(set, 0, None, oa_regs, "OA")
        if len(flex_regs) > 0:
            add_register_config(set, 0, None, flex_regs, "FLEX")

        mdapi_hw_config_hash = oa_registry.Registry.mdapi_hw_config_hash(mdapi_set)
        guid_hash = oa_registry.Registry.chipset_derive_hash(chipset.lower(),
                                                             mdapi_hw_config_hash)
        hw_config_hash = oa_registry.Registry.hw_config_hash(set)

        if guid_hash in guids:
            set.set('hw_config_guid', guids[guid_hash])
        else:
            print_err("WARNING: No GUID found for metric set " + chipset + ", " + set_symbol_name + " (SKIPPING)")
            print_err("WARNING: If this is a new config add the following to guids.xml:")
            print_err("<guid config_hash=\"" + hw_config_hash + "\" mdapi_config_hash=\"" + mdapi_hw_config_hash + "\" id=\"" + str(uuid.uuid4()) + "\" chipset=\"" + chipset.lower() + "\" name=\"" + set_symbol_name + "\" />")
            metrics.remove(set)
            continue


        sets[set_symbol_name] = set

        counters = {}
        normalization_equations = {}
        raw_equations = {}

        # Awkwardly we can't assume metrics are in dependency order and have to
        # sort them manually. We start by associating a list of dependencies with
        # each counter...

        mdapi_counters = {}
        mdapi_counter_deps = {}

        for mdapi_counter in mdapi_set.findall("Metrics/Metric"):
            symbol_name = mdapi_counter.get('SymbolName')

            if symbol_name in counter_blacklist:
                continue;

            # Have seen at least one MetricSet with a duplicate GpuCoreClocks counter...
            if symbol_name in mdapi_counters:
                print_err("WARNING: Skipping duplicate counter \"" + symbol_name + \
                        "\" in " + set.get('name') + " :: " + mdapi_counter.get('ShortName'))
                continue;

            deps = []
            equations = fixup_equation(str(mdapi_counter.get('SnapshotReportReadEquation'))) + " " + \
                        fixup_equation(str(mdapi_counter.get('SnapshotReportDeltaEquation'))) + " " + \
                        fixup_equation(str(mdapi_counter.get('DeltaReportReadEquation'))) + " " + \
                        fixup_equation(str(mdapi_counter.get('NormalizationEquation')))
            equations = expand_macros(equations)
            equations = equations.replace('$$', "$")
            for token in equations.split():
                if token[0] == '$' and token[1:] not in sys_vars and token[1:] != "Self":
                    deps.append(token[1:])

            mdapi_counters[symbol_name] = mdapi_counter
            mdapi_counter_deps[symbol_name] = deps

        sorted_mdapi_counters = sort_counters(mdapi_counters, mdapi_counter_deps)

        for mdapi_counter in sorted_mdapi_counters:

            aliases = mdapi_counter.get('Alias')

            skip_counter = False

            # We don't currently support configuring and reading perfmon registers
            signal = mdapi_counter.get('SignalName')
            if signal and "perfmon" in signal:
                continue;

            # A few things to fixup with this common counter...
            if mdapi_counter.get('SymbolName') == "AvgGpuCoreFrequencyMHz":
                # To avoid requiring a special case in tools, add a max value
                # equation for the gpu frequency...
                mdapi_counter.set('MaxValueEquation', "$GpuMaxFrequency")

                # Don't include units in the name
                mdapi_counter.set('SymbolName', "AvgGpuCoreFrequency")

                # Use canonical, first order of magnitude units specifier
                mdapi_counter.set('MetricUnits', 'Hz')
                mdapi_counter.set('NormalizationEquation', '$GpuCoreClocks 1000000000 UMUL $GpuTime UDIV')
                #mdapi_counter.set('DeltaReportReadEquation', '$GpuCoreClocks $GpuTime UDIV')


            symbol_name = mdapi_counter.get('SymbolName')

            counter = et.SubElement(set, 'counter')
            counter.set('name', apply_aliases(mdapi_counter.get('ShortName'), aliases))
            counter.set('symbol_name', mdapi_counter.get('SymbolName'))
            counter.set('underscore_name', underscore(mdapi_counter.get('SymbolName')))
            counter.set('description', apply_aliases(mdapi_counter.get('LongName'), aliases))
            counter.set('mdapi_group', apply_aliases(to_text(mdapi_counter.get('Group')), aliases))
            counter.set('mdapi_usage_flags', to_text(mdapi_counter.get('UsageFlags')))
            counter.set('mdapi_supported_apis', strip_dx_apis(mdapi_counter.get('SupportedAPI')))
            low = mdapi_counter.get('LowWatermark')
            if low:
                counter.set('low_watermark', low)
            high = to_text(mdapi_counter.get('HighWatermark'))
            if high:
                counter.set('high_watermark', high)
            counter.set('data_type', mdapi_counter.get('ResultType').lower())

            max_eq = fixup_equation(mdapi_counter.get('MaxValueEquation'))
            if max_eq:
                counter.set('max_equation', max_eq)

            # XXX Not sure why EU metrics tend to just be bundled under 'gpu'
            counter.set('mdapi_hw_unit_type', mdapi_counter.get('HWUnitType').lower())

            # There are counters representing cycle counts that have a semantic
            # type of 'duration' which doesn't seem to make sense...
            units = mdapi_counter.get('MetricUnits').lower()
            if units == "cycles":
                semantic_type = "event"
            else:
                semantic_type = mdapi_counter.get('MetricType').lower()

            counter.set('units', units)
            counter.set('semantic_type', semantic_type)

            # MDAPI MetricSets have 3 different kinds of counter read equations:
            #
            # 1) One for reading a raw (unnormalized) value from a hardware report
            #
            #       The line between normalized and raw isn't always clear
            #       as the raw equation may e.g. read and ADD multiple counters
            #
            #       Not all counters have a raw equation if they are instead
            #       derived through $CounterName references to other counters
            #       in a normalized value equation
            #
            # 2) One for reading an unnormalized value from the accumulated 'delta reports'
            #
            #       Seems to duplicate the raw equation but with delta report
            #       offsets and referencing 64bit values
            #
            #       The normalized value equations are always based on these
            #       accumulated delta values
            #
            # 3) One for reading a normalized value
            #
            #       These may start with a reference to "$Self" which is
            #       effectively a macro for the above delta report equation
            #
            #       If this is missing the delta report equation is effectively
            #       the normalized equation too
            #
            #       XXX: Beware that there are some inconsistent counters that
            #       have a normalization equation with a $Self reference and a
            #       raw equation but no delta report equation. This seems
            #       pretty sketchy, but (at least for 'MEDIA' metrics) we will
            #       substitute the raw equation for $Self in this case along
            #       with a warning to double check the results.
            #
            # Currently there doesn't appear to be a clear reason to
            # differentiate these equations and the separation seems to
            # complicate things for tools wanting to generate code from this
            # data.
            #
            # We instead aim to have one normalized equation per counter that
            # always reference accumulated counter values.

            # XXX: As a special case, we override the raw and delta report
            # equations for the GpuTime counters, which seem inconsistent
            if mdapi_counter.get('SymbolName') == "GpuTime":
                mdapi_counter.set('SnapshotReportReadEquation', "dw@0x04 1000000000 UMUL $GpuTimestampFrequency UDIV")
                mdapi_counter.set('DeltaReportReadEquation', "qw@0x0 1000000000 UMUL $GpuTimestampFrequency UDIV")

            availability = fixup_equation(mdapi_counter.get('AvailabilityEquation'))
            if availability == "":
                availability = None

            # We prefer to only look at the equations that reference the raw
            # reports since the mapping of offsets back to A,B,C counters is
            # unambiguous, but if necessary we will fallback to mapping
            # delta report offsets (accumulated 64bit values that correspond
            # to the 32bit or 40bit values from raw repots)

            raw_read_eq = fixup_equation(mdapi_counter.get('SnapshotReportReadEquation'))
            if raw_read_eq:
                if raw_read_eq == "":
                    raw_read_eq = None
                else:
                    raw_read_eq = replace_read_tokens_with_rpn_read_ops(chipset,
                                                                        raw_read_eq,
                                                                        True) #raw offsets

            delta_read_eq = fixup_equation(mdapi_counter.get('DeltaReportReadEquation'))
            if delta_read_eq:
                if delta_read_eq == "":
                    delta_read_eq = None
                else:
                    delta_read_eq = replace_read_tokens_with_rpn_read_ops(chipset,
                                                                          delta_read_eq,
                                                                          False) #delta offsets

            if raw_read_eq and not delta_read_eq:
                print_err("WARNING: Counter with raw equation but no delta report equation: MetricSet=\"" + \
                          mdapi_set.get('ShortName') + "\" Metric=\"" + mdapi_counter.get('SymbolName') + \
                          "(" + mdapi_counter.get('ShortName') + ")" + "\"")
                # Media metric counters currently have no delta equation even
                # though they have normalization equations that reference $Self
                if "MEDIA" in apis:
                    print_err("WARNING: -> Treating inconsistent media metric's 'raw' equation as a 'delta report' equation, but results should be double checked!")
                    delta_read_eq = raw_read_eq
                else:
                    set.remove(counter)
                    continue

            # Some counters are sourced from register values that are
            # not put into the OA reports. This is why some counters
            # will have a delta equation but not a raw equation. These
            # counters are typically only available in query mode. For
            # this reason we put a particular availability value.
            if delta_read_eq and not raw_read_eq:
                assert availability == None
                availability = "true $QueryMode &&"
                raw_read_eq = delta_read_eq

            # After replacing read tokens with RPN counter READ ops the raw and
            # delta equations are expected to be identical so warn if that's
            # not true...
            if bool(raw_read_eq) ^ bool(delta_read_eq) or raw_read_eq != delta_read_eq:
                print_err("WARNING: Inconsistent raw and delta report equations for " + \
                          mdapi_set.get('ShortName') + " :: " + mdapi_counter.get('SymbolName') + \
                          "(" + mdapi_counter.get('ShortName') + ")" + ": raw=\"" + str(raw_read_eq) + \
                          "\" delta=\"" + str(delta_read_eq) + "\" (SKIPPING)")
                set.remove(counter)
                continue

            normalize_eq = fixup_equation(mdapi_counter.get('NormalizationEquation'))
            if normalize_eq and normalize_eq == "":
                normalize_eq = None

            if normalize_eq:
                # Some normalization equations are represented with macros such as
                # 'GpuDuration' corresponding to:
                #
                #   "$Self 100 UMUL $GpuCoreClocks FDIV"
                #
                # We expand macros here so tools don't need to care about them...
                #
                equation = normalize_eq
                equation = expand_macros(equation)
                if raw_read_eq:
                    equation = equation.replace('$Self', raw_read_eq)
            else:
                equation = delta_read_eq

            if '$Self' in equation:
                print_err("WARNING: Counter equation (\"" + equation + "\") with unexpanded $Self token: MetricSet=\"" + \
                          mdapi_set.get('ShortName') + "\" Metric=\"" + mdapi_counter.get('SymbolName') + \
                          "(" + mdapi_counter.get('ShortName') + ")" + "\" (SKIPPING)")
                set.remove(counter)
                continue

            # $$CounterName vs $CounterName in an equation is intended to
            # differentiate referencing the normalized or raw value of another
            # counter.
            #
            # Since we are only keeping a single (normalized) equation for
            # counters we only need one form, but we want to be careful to
            # check if any equations really depend on the raw value of another
            # counter so we can expand those variables now
            #
            tmp = equation
            for token in tmp.split():
                if token[0] == '$' and token[1] != '$':
                    if token[1:] in normalization_equations:
                        raw_eq = raw_equations[token[1:]]

                        equation = equation.replace(token, raw_eq)
                        #if token[1:] not in raw_equations:
                        #   print_err("WARNING: Counter equation (\"" + equation + "\") references un-kept raw equation of another counter : MetricSet=\"" + \
                        #             mdapi_set.get('ShortName') + "\" Metric=\"" + mdapi_counter.get('ShortName') + "\"")

                    elif token[1:] not in raw_equations and token[1:] not in sys_vars:
                        print_err("Unknown variable name: \"" + token + "\" in equation \"" + equation + "\"")

            symbol_name = counter.get('symbol_name')

            # Make sure that every variable in the equation is a known sys_var or counter name
            equation = equation.replace('$$', "$")
            for token in equation.split():
                if token[0] == '$':
                    if token[1:] not in counters and token[1:] not in sys_vars:
                        print_err("WARNING: Counter equation (\"" + equation + "\") with unknown variable " + \
                                  token + " (maybe skipped counter): MetricSet=\"" + mdapi_set.get('ShortName') + \
                                  "\" Metric=\"" + mdapi_counter.get('SymbolName') + "(" + mdapi_counter.get('ShortName') + \
                                  ")" + "\" (SKIPPING)")
                        set.remove(counter)
                        skip_counter = True
                        break

            if skip_counter:
                continue

            counter.set('equation', equation.strip())

            if availability != None:
                counter.set('availability', availability)

            counters[symbol_name] = counter;
            if normalize_eq:
                normalization_equations[symbol_name] = normalize_eq
            if raw_read_eq:
                raw_equations[symbol_name] = raw_read_eq


if args.dry_run:
    sys.exit(0)

# Merge in any custom meta data we have...
if args.merge:
    merge = et.parse(args.merge)
    merge_metrics = merge.getroot()

    for merge_set in merge.findall(".//set"):
        pattern = ".//set[@symbol_name=\"" + merge_set.get('symbol_name') + "\"][@chipset=\"" + merge_set.get('chipset') + "\"]"
        real_set = metrics.find(pattern)
        if real_set is not None:
            for set_attr in merge_set.items():
                real_set.set(set_attr[0], set_attr[1])

            for merge_elem in merge_set:
                if merge_elem.tag == "counter":
                    merge_counter = merge_elem
                    pattern = "counter[@symbol_name=\"" + merge_counter.get('symbol_name') + "\"]"
                    real_counter = real_set.find(pattern)
                    if real_counter is not None:
                        for counter_attr in merge_counter.items():
                            real_counter.set(counter_attr[0], counter_attr[1])
                    else:
                        real_set.append(merge_counter)
                        real_counter = merge_counter
                else:
                    real_set.append(merge_elem)

    # For consistency + readability print everything manually...
    merge_md5 = hashlib.md5(open("merge.xml", 'rb').read()).hexdigest()
else:
    merge_md5 = ""

print ("<?xml version=\"1.0\"?>")
print("<metrics version=\"" + str(int(time.time())) + "\" merge_md5=\"" + merge_md5 + "\">")
for set in metrics.findall(".//set"):
    print("  <set name=\"" + set.get('name') + "\"")
    del set.attrib['name']
    for attr in set.items():
        print("       " + attr[0] + "=\"" + attr[1] + "\"")
    print("       >")
    for counter in set.findall("counter"):
        print("    <counter name=\"" + counter.get('name') + "\"")
        del counter.attrib['name']
        for attr in counter.items():
            if attr[0][:6] != "mdapi_":
                print("             " + attr[0] + "=\"" + saxutils.escape(attr[1]) + "\"")
        for attr in counter.items():
            if attr[0][:6] == "mdapi_":
                print("             " + attr[0] + "=\"" + saxutils.escape(attr[1]) + "\"")
        print("             />")
    for config in set.findall("register_config"):
        if config.get('availability') != None:
            print("    <register_config type=\"" + config.get('type') + "\"")
            print("                     availability=\"" + saxutils.escape(config.get('availability')) + "\"")
            print("                     priority=\"" + config.get('priority') + "\"")
            print("                     >")
        else:
            print("    <register_config type=\"" + config.get('type') + "\">")
        for reg in config.findall("register"):
            addr = int(reg.get('address'), 16)

            if 'registers' in chipsets[chipset] and addr in chipsets[chipset]['registers']:
                reg_info = chipsets[chipset]['registers'][addr]
                comment = ' <!--' + reg_info['name'] + ' -->'
            else:
                comment = ''

            print("        <register type=\"" + reg.get('type') + "\" address=\"" + reg.get('address') + "\" value=\"" + reg.get('value') + "\" />" + comment)
        print("    </register_config>")
    print("  </set>\n")
print("</metrics>")
