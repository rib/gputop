
import copy
import hashlib

import xml.etree.ElementTree as et


class Registry:

    # Tries to avoid fragility from et.tostring() by normalizing into CSV string first
    @staticmethod
    def hw_config_hash(metric_set):
        """Hashes the given metric set's HW register configs.

        Args:
            metric_set -- is an ElementTree element for a 'set'

        Note this doesn't accept an MDAPI based metric set description
        """

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

        return hashlib.md5(registers_str.encode('utf-8')).hexdigest()


    @staticmethod
    def mdapi_hw_config_hash(mdapi_metric_set):
        """Hashes the HW register configuration of a metric set from VPG's MDAPI XML files.

        Args:
            mdapi_metric_set -- is an ElementTree element for a 'MetricSet'

        Note: being a simplistic hash of all RegConfigStart element contents
        this will change for minor comment changes in VPG's files. Without
        any promisies of stability within these files then it can't help to
        err on the side of caution here, so we know when to investigate
        changes that might affect our useages.
        """

        config = et.Element('config')
        for registers in mdapi_metric_set.findall(".//RegConfigStart"):
            config.append(copy.deepcopy(registers))
        registers_str = et.tostring(config)

        return hashlib.md5(registers_str).hexdigest()


    @staticmethod
    def chipset_derive_hash(chipset, hash):
        """Derive a HW config hash for a given chipset.

        This helps us avoiding collisions with identical config across
        different Gen or GT.
        """

        return "%s-%s" % (chipset, hash)
