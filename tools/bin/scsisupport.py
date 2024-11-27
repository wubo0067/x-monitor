import subprocess
import os
import re

PCIIDS = '/usr/share/pci.ids'
DRVDIR = 'drivers/scsi'

# Build pci ids database
pci_ids = {}

with open(PCIIDS) as inf:
    for entry in inf:
        entry = entry.rstrip()
        if not entry or entry[0] == '#':
            continue
        if entry.startswith('\t\t'):
            subvendor, subdev, subsystem_name = entry.lstrip().split(None, 2)
            subvendor = int(subvendor, 16)
            subdev = int(subdev, 16)

            pci_ids[vendor, device][subvendor, subdev] = subsystem_name
        elif entry.startswith('\t'):
            device, device_name = entry.lstrip().split(None, 1)
            device = int(device, 16)
            device_name = ' '.join((vendor_name, device_name))

            pci_ids[vendor, device] = {'device': device_name}
        else:
            vendor, vendor_name = entry.lstrip().split(None, 1)
            vendor = int(vendor, 16)

            if vendor == 0xffff:
                # Assuming that 'pci.ids' is sorted, ignore the rest of file
                # (should only list of classes)
                break

# Walk over SCSI drivers and find matching drivers
re_alias = re.compile('alias:\s*pci:v([0-9A-F]+)d([0-9A-F]+)sv([0-9A-F]+|\*)sd([0-9A-F]+|\*)')

_, _, release, _, _ = os.uname()

modules_dir = os.path.join('/lib/modules', release, 'kernel', DRVDIR)

for topdir, dirs, files in os.walk(modules_dir):
    for fname in files:
        if fname.endswith('.ko'):
            print '####', fname

            # Check for supported devices
            proc = subprocess.Popen('/usr/sbin/modinfo {}'.format(os.path.join(topdir, fname)),
                                    shell = True,
                                    stdout = subprocess.PIPE)
            for line in proc.stdout:
                m = re_alias.match(line)

                if not m:
                    continue

                try:
                    vendor, device, subvendor, subdevice = m.groups()

                    vendor = int(vendor, 16)
                    device = int(device, 16)
                    subvendor = int(subvendor, 16) if subvendor != '*' else None
                    subdevice = int(subdevice, 16) if subdevice != '*' else None

                    pciid = pci_ids[vendor, device]
                    print pciid['device']

                    if subvendor and subdevice:
                        print pciid[subvendor, subdevice]
                    elif subvendor:
                        for key, subdevice_name in pciid.items():
                            if key != 'device' and key[0] == subvendor:
                                print subdevice_name
                except KeyError as ke:
                    print 'alias {} -- not found'.format(m.groups())
                except Exception as e:
                    pass
