# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
This scripts allows to get basic stats about the inspect.json file contained in a snapshot.zip file.

To use you need to have a snapshot.zip file either generated by `ffx inspect` or from a crash report.

Example usage:

```
$ unzip /path/to/snapshot.zip
$ python3 snapshot_inspect_stats.py --sort nodes /path/to/unzipped/inspect.json
```
"""

import argparse
import json
import sys


def measure(obj, depth=0):
    """
    Returns the number of nodes, properties and the depth of an inspect tree.
    `obj` is a dict read from JSON that represents inspect data
    """
    nodes = 0
    properties = 0
    max_depth = depth
    for (_, child) in obj.items():
        # ensure this is a node that is not a histogram
        if type(child) is dict and 'buckets' not in child:
            (child_nodes, child_properties, child_depth) = measure(
                child, depth=depth + 1)
            nodes += child_nodes + 1
            properties += child_properties
            max_depth = max(max_depth, child_depth)
            continue
        properties += 1
    return nodes, properties, max_depth


def get_sort_index(sort_opt):
    if args.sort == 'nodes':
        return 0
    elif args.sort == 'properties':
        return 1
    elif args.sort == 'depth':
        return 2


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Show stats about inspect data in a snapshot.')
    parser.add_argument(
        'filepath', metavar='FILE', type=str, help='path to inspect.json file')
    parser.add_argument(
        '--sort',
        dest='sort',
        choices=['depth', 'nodes', 'properties'],
        default='nodes',
        help='based on what the output will be sorted')

    args = parser.parse_args(sys.argv[1:])

    with open(args.filepath) as f:
        data = json.load(f)
        results = [
            (response['moniker'], measure(response['payload']))
            for response in data
            if response['payload']
        ]
        sort_index = get_sort_index(args.sort)
        results.sort(key=lambda v: v[1][sort_index], reverse=True)
        print('MONIKER NODES PROPERTIES DEPTH')
        for (moniker, (nodes, properties, depth)) in results:
            print('{:<20} {} {} {}'.format(moniker, nodes, properties, depth))
