#!/usr/bin/env python2

# Copyright (C) 2016 Intel Corporation
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

# This script can:
#
# - Automatically add template entries for unregistered metric sets diescovered
#   in new mdapi xml files.
# - Once mdapi-convert-xml.py has been run to output register configs for new
#   metric sets then re-running this script can add the 'v2' config hash to
#   corresponding registry entries.
#
# The script is designed to allow incremental updates/fixups of the guid
# registry by working in terms of:
#
# 1) load all the existing state
# 2) apply tweaks/modifications
# 3) write everything back out
#
# The script should gracefully handle incomplete guid entries, which is
# important when considering how the mdapi-xml-convert.py script depends on the
# 'v1' mdapi config hash while the second 'v2' hash depends on the configs
# output by mdapi-xml-convert.py.



import xml.etree.ElementTree as ET
import xml.sax.saxutils as saxutils
import time
import sys
import re
import argparse
import hashlib
import copy
import uuid


def print_err(*args):
    sys.stderr.write(' '.join(map(str,args)) + '\n')


# The V1 hash is based on a hash of the register configs as described in
# MDAPI XML files.
def get_v1_config_hash(mdapi_metric_set):
    config = ET.Element('config')
    for registers in mdapi_metric_set.findall(".//RegConfigStart"):
        config.append(copy.deepcopy(registers))
    registers_str = ET.tostring(config)

    return hashlib.md5(registers_str).hexdigest()


# The V2 hash is based on a hash of the register configs as described in
# oa-*.xml files output by mdapi-xml-convert.py
#
# Tries to avoid fragility from ET.tostring() by normalizing into CSV string first
# FIXME: avoid copying between scripts!
def get_v2_config_hash(metric_set):
    registers_str = ""
    for config in metric_set.findall(".//register_config"):
        if config.get('id') == None:
            config_id = '0'
        else:
            config_id = config.get('id')
        if config.get('priority') == None:
            config_priority = '0'
        else:
            config_priority = config.get('priority')
        if config.get('availability') == None:
            config_availability = ""
        else:
            config_availability = config.get('availability')
        for reg in config.findall("register"):
            addr = int(reg.get('address'), 16)
            value = int(reg.get('value'), 16)
            registers_str = registers_str + config_id + ',' + config_priority + ',' + config_availability + ',' + str(addr) + ',' + str(value) + '\n'

    return hashlib.md5(registers_str).hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("xml", nargs="+", help="XML description of metrics")
parser.add_argument("--guids", required=True, help="Metric set GUID registry")

args = parser.parse_args()


guids = []
guid_index = {} # guid objects indexed by id
v1_guid_table = {} # indexed by the v1 hash
named_guid_table = {} # indexed by name=<chipset>_<symbol_name>



# 1) read everything we have currently
#
guids_xml = ET.parse(args.guids)
for guid in guids_xml.findall(".//guid"):
    guid_obj = {}
    guid_obj['id'] = guid.get('id')

    if guid.get('mdapi_config_hash') != None:
        guid_obj['v1_hash'] = guid.get('mdapi_config_hash')
    if guid.get('config_hash') != None:
        guid_obj['v2_hash'] = guid.get('config_hash')

    if guid.get('chipset') != None:
        guid_obj['chipset'] = guid.get('chipset')
    if guid.get('name') != None:
        guid_obj['name'] = guid.get('name')
        named_guid_table[guid_obj['chipset'] + "_" + guid_obj['name']] = guid_obj

    v1_guid_table[guid_obj['v1_hash']] = guid_obj

    guids.append(guid_obj)

    if guid_obj['id'] in guid_index:
        print_err("Duplicate GUID " + guid_obj['id'] + "!")
        sys.exit(1)
    guid_index[guid_obj['id']] = guid_obj


#
# 2) fixup/modify the guid entries...
#


for arg in args.xml:
    internal = ET.parse(arg)
    for internal_set in internal.findall(".//MetricSet"):

        v1_hash = get_v1_config_hash(internal_set)

        chipset = internal_set.get('SupportedHW').lower()
        set_name = internal_set.get('SymbolName')

        if v1_hash in v1_guid_table:
            guid_obj = v1_guid_table[v1_hash]

            guid_obj['name'] = set_name
            guid_obj['chipset'] = chipset
            guid_obj['matched_mdapi'] = True
        else:
            guid_obj = { 'v1_hash': v1_hash,
                         'id': str(uuid.uuid4()),
                         'name': set_name,
                         'chipset': chipset,
                         'unregistered': True,
                         'matched_mdapi': True
                       }
            guid_index[guid_obj['id']] = guid_obj
            v1_guid_table[guid_obj['v1_hash']] = guid_obj
            guids.append(guid_obj)
            print_err("Unregistered GUID for metric set = " + set_name + " (" + chipset + ")")

        named_guid_table[chipset + '_' + set_name] = guid_obj



chipsets = [ 'hsw', 'bdw', 'chv', 'skl', 'bxt' ]

for chipset in chipsets:
    public = ET.parse('oa-' + chipset + '.xml')

    for metricset in public.findall(".//set"):

        set_name = metricset.get('symbol_name')

        v2_hash = get_v2_config_hash(metricset)

        guid_key = chipset + "_" + set_name
        if guid_key in named_guid_table:
            guid_obj = named_guid_table[guid_key]
            guid_obj['v2_hash'] = v2_hash


#
# 3) write all the guids back out...

print("<guids>")
for guid_obj in guids:
    comment = None
    line = "<guid"

    if 'matched_mdapi' not in guid_obj:
        comment = "MDAPI register hash lookup failed! (maybe it was removed or the config was changed?)"
        print_err("WARNING: guid = " + guid_obj['id'] + "  " + comment)

    if 'v2_hash' in guid_obj:
        line = line + ' config_hash="' + guid_obj['v2_hash'] + '"'    
    if 'v1_hash' in guid_obj:
        line = line + ' mdapi_config_hash="' + guid_obj['v1_hash'] + '"'

    line = line + ' id="' + guid_obj['id'] + '"'

    if 'chipset' in guid_obj:
        line = line + ' chipset="' + guid_obj['chipset'] + '"'

    if 'name' in guid_obj:
        line = line + ' name="' + guid_obj['name'] + '"'

    line = line + ' />'

    if 'unregistered' in guid_obj:
        line = "<!-- " + line + " -->"
    if comment != None:
        line = line + " <!-- " + comment + " -->"

    print("    " + line)
print("</guids>")
