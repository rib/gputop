#!/usr/bin/env python2
# coding=utf-8

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
#   metric sets then re-running this script can add the config_hash attribute
#   to corresponding registry entries.
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
# 'mdapi_config_hash' attribute while adding the 'config_hash' attribute
# depends on the configs output by mdapi-xml-convert.py.



import argparse
import os.path
import re
import sys
import time
import uuid

import xml.etree.ElementTree as et
import xml.sax.saxutils as saxutils

import pylibs.oa_guid_registry as oa_registry


def print_err(*args):
    sys.stderr.write(' '.join(map(str,args)) + '\n')

def guid_hashing_key(guid_obj):
    return oa_registry.Registry.chipset_derive_hash(guid_obj['chipset'],
                                                    guid_obj['mdapi_config_hash'])

parser = argparse.ArgumentParser()
parser.add_argument("xml", nargs="+", help="XML description of metrics")
parser.add_argument("--guids", required=True, help="Metric set GUID registry")

args = parser.parse_args()


guids = []
guid_index = {} # guid objects indexed by id
mdapi_config_hash_guid_table = {} # indexed by MDAPI XML register config hash
named_guid_table = {} # indexed by name=<chipset>_<symbol_name>



# 1) read everything we have currently
#
guids_xml = et.parse(args.guids)
for guid in guids_xml.findall(".//guid"):
    guid_obj = {}

    if guid.get('id') != None:
        guid_obj['id'] = guid.get('id')
    else:
        guid_obj['id'] = str(uuid.uuid4())

    if guid.get('mdapi_config_hash') != None:
        guid_obj['mdapi_config_hash'] = guid.get('mdapi_config_hash')
    if guid.get('config_hash') != None:
        guid_obj['config_hash'] = guid.get('config_hash')

    if guid.get('chipset') != None:
        guid_obj['chipset'] = guid.get('chipset')
    if guid.get('name') != None:
        guid_obj['name'] = guid.get('name')
        named_guid_table[guid_obj['chipset'] + "_" + guid_obj['name']] = guid_obj

    if 'mdapi_config_hash' in guid_obj:
        hashing_key = oa_registry.Registry.chipset_derive_hash(guid_obj['chipset'],
                                                               guid_obj['mdapi_config_hash'])
        mdapi_config_hash_guid_table[hashing_key] = guid_obj

    guids.append(guid_obj)

    if guid_obj['id'] in guid_index:
        print_err("Duplicate GUID " + guid_obj['id'] + "!")
        sys.exit(1)
    guid_index[guid_obj['id']] = guid_obj


#
# 2) fixup/modify the guid entries...
#


for arg in args.xml:
    internal = et.parse(arg)

    concurrent_group = internal.find(".//ConcurrentGroup")

    for mdapi_set in internal.findall(".//MetricSet"):

        mdapi_config_hash = oa_registry.Registry.mdapi_hw_config_hash(mdapi_set)

        chipset = mdapi_set.get('SupportedHW').lower()
        if concurrent_group.get('SupportedGT') != None:
            chipset = chipset + concurrent_group.get('SupportedGT').lower()

        set_name = mdapi_set.get('SymbolName')

        name = chipset + "_" + set_name;

        hashing_key = oa_registry.Registry.chipset_derive_hash(chipset, mdapi_config_hash)
        if hashing_key in mdapi_config_hash_guid_table:
            guid_obj = mdapi_config_hash_guid_table[hashing_key]

            guid_obj['name'] = set_name
            guid_obj['chipset'] = chipset
            guid_obj['matched_mdapi'] = True
        elif name in named_guid_table:
            guid_obj = named_guid_table[name]

            guid_obj['matched_mdapi'] = True
            guid_obj['mdapi_config_hash'] = mdapi_config_hash
            if 'config_hash' in guid_obj:
                del guid_obj['config_hash']
            guid_obj['comment'] = "WARNING: MDAPI XML config hash changed! If upstream, double check raw counter semantics unchanged"
            print_err("WARNING: MDAPI XML config hash changed for \"" + set_name + "\" (" + chipset + ") If upstream, double check raw counter semantics unchanged")
        else:
            guid_obj = { 'mdapi_config_hash': mdapi_config_hash,
                         'id': str(uuid.uuid4()),
                         'name': set_name,
                         'chipset': chipset,
                         'unregistered': True,
                         'matched_mdapi': True,
                         'comment': "New"
                       }
            guid_index[guid_obj['id']] = guid_obj
            mdapi_config_hash_guid_table[guid_hashing_key(guid_obj)] = guid_obj
            guids.append(guid_obj)
            print_err("New GUID \"" + guid_obj['id'] + "\" for metric set = " + set_name + " (" + chipset + ")")

        named_guid_table[chipset + '_' + set_name] = guid_obj



chipsets = [ 'hsw',
             'bdw', 'chv',
             'sklgt2', 'sklgt3', 'sklgt4', 'kblgt2', 'kblgt3', 'cflgt2', 'cflgt3',
             'bxt', 'glk',
             'cnl',
             'icl', 'lkf',
             'tgl']

for chipset in chipsets:
    filename = 'oa-' + chipset + '.xml'
    if not os.path.isfile(filename):
        continue

    public = et.parse(filename)

    for metricset in public.findall(".//set"):

        set_name = metricset.get('symbol_name')

        config_hash = oa_registry.Registry.hw_config_hash(metricset)

        guid_key = chipset + "_" + set_name
        if guid_key in named_guid_table:
            guid_obj = named_guid_table[guid_key]
            guid_obj['config_hash'] = config_hash


#
# 3) write all the guids back out...

print("<guids>")
for guid_obj in guids:
    comment = None
    line = "<guid"

    if 'matched_mdapi' not in guid_obj:
        comment = "Not found in MDAPI XML file[s]; Entry copied unmodified (maybe removed from MDAPI XML or not all files given on command line)"

    if 'comment' in guid_obj:
        comment = guid_obj['comment']

    if 'config_hash' in guid_obj:
        line = line + ' config_hash="' + guid_obj['config_hash'] + '"'
    if 'mdapi_config_hash' in guid_obj:
        line = line + ' mdapi_config_hash="' + guid_obj['mdapi_config_hash'] + '"'

    line = line + ' id="' + guid_obj['id'] + '"'

    if 'chipset' in guid_obj:
        line = line + ' chipset="' + guid_obj['chipset'] + '"'

    if 'name' in guid_obj:
        line = line + ' name="' + guid_obj['name'] + '"'

    line = line + ' />'

    if comment != None:
        print("    <!-- ↓" + comment + " -->")

    print("    " + line)
print("</guids>")
