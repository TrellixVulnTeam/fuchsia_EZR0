#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Reads the contents of a manifest file generated by the build and verifies
   that there are no collisions among destination paths.
   '''

import argparse
import json
import sys

# Import from the current directory.
import distribution_manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--input', help='Path to original manifest', required=True)
    parser.add_argument(
        '--output', help='Path to the updated manifest', required=True)
    parser.add_argument(
        '--depfile', help='Path to GN style depfile', required=True)
    args = parser.parse_args()

    with open(args.input, 'r') as input_file:
        contents = json.load(input_file)

    opened_files = set()
    entries, error = distribution_manifest.expand_manifest(
        contents, opened_files)
    if error:
        print(error, file=sys.stderr)
        return 1

    with open(args.output, 'w') as output_file:
        json.dump(
            [e._asdict() for e in sorted(entries)],
            output_file,
            indent=2,
            sort_keys=True,
            separators=(',', ': '))

    with open(args.depfile, 'w+') as depfile:
        depfile.write('%s: %s\n' % (args.output, ' '.join(opened_files)))

    return 0


if __name__ == '__main__':
    sys.exit(main())
