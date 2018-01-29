# About guids.xml

This is the authoritive registry of unique identifers for different OA unit
hardware configurations. Userspace can reliably use these identifiers to map a
configuration to corresponding normalization equations and counter meta data.

If a hardware configuration ever changes in a backwards incompatible way
(changing the semantics and/or layout of the raw counters) then it must be
given a new GUID.

mdapi-xml-convert.py will match metric sets with a GUID from this file based on
an md5 hash of the hardware register configuration and skip a metric set with a
warning if no GUID could be found.

All new metric sets need to be allocated a GUID here before
mdapi-xml-convert.py or i915-perf-kernelgen.py will output anything for that
metric set. This ensures we don't automatically import new metric sets without
some explicit review that that's appropriate.

A failure to find a GUID for an older metric set most likely implies that the
register configuration was changed. It's possible that the change is benign
(e.g. a comment change) and in that case the mdapi_config_hash for the
corresponding metric set below can be updated.

The update-guids.py script is the recommended way of managing updates to this
file by generate a temporary file with proposed updates that you can compare
with the current guids.xml.


# update-guids.xml

update-guids.py can help with:

* Recognising new metrics from VPG's MDAPI XML files

  *(NOTE: new guids.xml entries will initially be missing the
  config_hash=MD5_HASH attribute until mdapi-xml-convert.py is used to generate
  a corresponding oa-*.xml config description)*

* Adding a config_hash=MD5_HASH attribute to recently added guids.xml entries
  after mdapi-xml-convert.py has been run.

* Allocating a GUID for a custom metric that doesn't have a counterpart in
  VPG's MDAPI XML files.

  For this case you can add a stub entry with only a name like `<guid
  name="Foo">` to guids.xml and then running update-guids.py will output a
  corresponding line with the addition of an id=UUID attribute.


# How to sync the oa-\*.xml files with latest internal MDAPI XML files

1. E.g. copy a new `MetricsXML_BDW.xml` to `mdapi/MetricsXML_BDW.xml`

*Note: that the `mdapi-xml-convert.py` script will only convert configs that
have a corresponding GUID entry within `guids.xml`. This check helps avoid
unintentionally publishing early, work-in-progress/pre-production configs.*

The `guids.xml` registry maps each, complex OA unit register configuration to a
unique ID that userspace can recognise and trust the semantics of raw counters
read using that configuration. (Just for reference, this is particularly
valuable for tools that capture raw metrics for later, offline processing since
the IDs effectively provide a compressed description of how to interpret the
data by providing an index into a database of high-level counter descriptions.)

The registry associates each ID with a hash of the HW register config as found in
MDAPI XML files ('mdapi_config_hash') and also with a hash of the HW config as
found in oa-\*.xml files ('config_hash'). The hashes used for lookups in the
registry also help detect when the register config for a pre-existing metric set
is updated. Note: these hashes are only for the low-level hardware configuration
so updates to counter descriptions used by fronted UIs won't affect indexing
here.

There is a chicken and egg situation when updating or adding new entries to
guids.xml since we can't hash the configs in oa-\*.xml until successfully running
mdapi-xml-convert.py which depends on a guids.xml registry entry first. The
update-guids.xml script will output registry entries without an oa-\*.xml config
hash if not available and can be re-run after mdapi-xml-convert.py to add the
missing hashes.

2. Now run:
```
../scripts/update-guids.py --guids=guids.xml mdapi/MetricsXML_BDW.xml > guids.xml2
```
*(note the script expects to find oa-\*.xml files in the current directory)*

Diff `guids.xml` and `guilds.xml2` (easiest with a side-by-side diff editor) and
review the registry changes. *Note: many lines will have a warning like `"Not
found in MDAPI XML file[s]..."` if `update-guids.xml` wasn't given all known
MDAPI XML files but in this case they can be ignored for all non-BDW configs.*

*Note: for any config that is already supported upstream in the i915 perf driver
we need to be careful if the hash for a metric set changes in case the semantics
for any raw counters were changed. The semantics of raw counters associated with
a given GUID form part of the drm i915 perf uapi contract and must remain
backwards compatible.*

If the diff shows any `mdapi_config_hash` changes for pre-existing (especially
upstream) configs you should review the MDAPI XML changes for the metric set and
verify the change just relates to a bug fix. If more substantial changes were
made which could mean we need to treat it as a new config. Handling the later
case is left as an exercise to the reader, since it hasn't happened so far :-D.
Assuming all the changes and new entries look good they can be copied into
`guids.xml`, removing any trailing comment left by `update-guids.py`.

3. Now run mdapi-xml-convert.py:
```
../scripts/mdapi-xml-convert.py --guids=guids.xml mdapi/MetricsXML_BDW.xml > oa-bdw.xml
```

4. We can now update new entries in guids.xml with a 'config_hash':
```
../scripts/update-guids.py --guids=guids.xml mdapi/MetricsXML_BDW.xml > guids.xml2
```
*(and again diff, check the changes and copy across)*

At this point other codegen can be done based on the update oa-\*.xml files such
as genreating new i915 perf configs:

```
../scripts/i915-perf-kernelgen.py --guids=guids.xml --chipset=bdw --c-out=i915_oa_bdw.c --h-out=i915_oa_bdw.h --sysfs oa-bdw.xml
```


