#!/usr/bin/env python3
"""Patch ArduinoBLE ATT.cpp descriptor parsing. Pass its filesystem path.
Creates a .before-xbox-discovery backup; refuses unexpected source versions.
"""
import pathlib
import sys

old = '''          uint16_t lengthPerDescriptor = responseBuffer[1] * 4;
          uint8_t uuidLen = 2;'''
new = '''          // Xbox workaround: Find Information format 2 uses 18-byte entries.
          if (respLength < 2 || (responseBuffer[1] != 1 && responseBuffer[1] != 2)) {
            return false;
          }
          uint8_t uuidLen = responseBuffer[1] == 1 ? 2 : 16;
          uint16_t lengthPerDescriptor = 2 + uuidLen;
          if (respLength < 2 + lengthPerDescriptor ||
              (respLength - 2) % lengthPerDescriptor != 0) {
            return false;
          }'''
old_advance = '''            reqStartHandle = rawDescriptor->handle + 1;
          }
        } else {'''
new_advance = '''            if (rawDescriptor->handle < reqStartHandle ||
                rawDescriptor->handle > reqEndHandle) return false;
            reqStartHandle = rawDescriptor->handle + 1;
          }
          if (reqStartHandle == 0 || reqStartHandle > reqEndHandle) break;
        } else {'''

def patch(source):
    if source.count(old) != 1 or source.count(old_advance) != 1:
        raise ValueError('Unexpected ArduinoBLE source; no changes made.')
    return source.replace(old, new).replace(old_advance, new_advance)

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('Usage: python3 tools/patch-arduinoble-discovery.py /path/to/ArduinoBLE/src/utility/ATT.cpp')
    path = pathlib.Path(sys.argv[1])
    source = path.read_text()
    if 'Xbox workaround: Find Information format 2' in source:
        sys.exit('Patch already applied.')
    updated = patch(source)
    backup = path.with_name(path.name + '.before-xbox-discovery')
    if backup.exists():
        sys.exit('Backup already exists; inspect it before retrying.')
    backup.write_text(source)
    path.write_text(updated)
    print('Patched', path, '\nBackup:', backup)
