#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A collection of functions to process distribution manifests. These are JSON
# files that contain a a list of objects, which follow the schema documented
# in //build/dist/distribution_manifest.gni

import collections
import filecmp
import json
import os

from typing import Any, Callable, Iterable, List, Set, DefaultDict, Dict, Optional, Tuple

# A namedtuple type used to model an entry from a distribution manifest, after
# expansion.
Entry = collections.namedtuple('Entry', ['destination', 'source', 'label'])

# This supports distribution manifest as JSON files which are lists of objects
# (dictionaries in Python), fully documented at:
# //docs/concepts/build_system/internals/manifest_formats.md
#
# Note that it is possible for an expanded manifest to have several entries
# for the same destination path. If all entries have the same source
# (either the same source path, or the same source content!), then they
# can be merged into a single one. Otherwise, it is a build error.
#


def expand_manifest_items(
        manifest_items: Iterable[dict],
        opened_files: Set[str],
        default_label: Optional[str] = None) -> List[Entry]:
    """Expand the content of a distribution manifest file.

    Note that this function does not try to de-duplicate identical entries.

    Args:
        manifest_items: A list of dictionaries, corresponding to the
            content of a manifest file.
        opened_files: A set of file paths, which will be updated with
            the paths of the files that have been read during expansion.
        default_label: An optional string that will be used as a default
            "label" value if an entry does not have one.
    Returns:
        An Entry list.
    """
    entries = []
    for item in manifest_items:
        if 'label' not in item:
            item['label'] = default_label
        if 'source' in item:
            entries.append(Entry(**item))
        elif 'file' in item:
            file_path = item['file']
            item_label = item['label']
            opened_files.add(file_path)
            with open(file_path) as data_file:
                data = json.load(data_file)
            entries += expand_manifest_items(data, opened_files, item_label)
    return entries


def _entries_have_same_source(
        entry1: Entry, entry2: Entry, opened_files: Set[str]) -> bool:
    """Return True iff two entries have the same source."""
    if entry1.source == entry2.source:
        return True

    opened_files.add(entry1.source)
    opened_files.add(entry2.source)
    return filecmp.cmp(entry1.source, entry2.source)


def expand_manifest(manifest_items: Iterable[dict],
                    opened_files: Set[str]) -> Tuple[List[Entry], str]:
    """Expand the content of a distribution manifest into an Entry list.

    Note, this removes duplicate entries, if they have the same source
    path or content, and will report conflicts otherwise.

    Args:
        input_entries: An Entry list, that may contain duplicate entries.
            Note that two entries are considered duplicates if they
            have the same destination, and the same source (either by
            path or content).
        opened_files: A set of file paths, which will be updated with
            the paths of the files that have been read during the merge.
    Returns:
        A (merged_entries, error_msg) tuple, where merged_entries is an
        Entry list of the merged input entries, and error_msg is a string
        of error messages (which happen when conflicts are detected), or
        an empty string in case of success.
    """
    input_entries = expand_manifest_items(manifest_items, opened_files)

    # Map a destination path to a set of corresponding entries. This is
    # used later to determine conflicts and merge duplicates.
    entries_dict: DefaultDict[str, Set[Entry]] = collections.defaultdict(set)
    for entry in input_entries:
        dest = entry.destination
        entries_dict[dest].add(entry)

    # Used to record that a given destination path has two or more conflicting
    # entries, with different sources.
    source_conflicts: DefaultDict[str,
                                  Set[Entry]] = collections.defaultdict(set)

    dest_to_entries: Dict[str, Entry] = {}
    for entry in input_entries:
        dest = entry.destination
        current_entry = dest_to_entries.setdefault(dest, entry)
        if current_entry == entry:
            continue

        if not _entries_have_same_source(entry, current_entry, opened_files):
            source_conflicts[dest].update((current_entry, entry))
            continue

        # These entries have the same source path, so merge them.
        if current_entry.label is None:
            dest_to_entries[dest] = current_entry._replace(label=entry.label)

    error = ""
    for dest, entries in source_conflicts.items():
        error += "  Conflicting source paths for destination path: %s\n" % dest
        for entry in sorted(entries, key=lambda x: x.source):
            error += "   - source=%s label=%s\n" % (entry.source, entry.label)

    if error:
        error = 'ERROR: Conflicting distribution entries!\n' + error

    return (
        sorted(dest_to_entries.values(), key=lambda x: x.destination), error)


def distribution_entries_to_string(entries: List[Entry]) -> str:
    """Convert an Entry list to a JSON-formatted string."""
    return json.dumps(
        [e._asdict() for e in sorted(entries)],
        indent=2,
        sort_keys=True,
        separators=(',', ': '))


def convert_fini_manifest_to_distribution_entries(
        fini_manifest_lines: Iterable[str], label: str) -> List[Entry]:
    """Convert a FINI manifest into an Entry list.

    Args:
        fini_manifest_lines: An iteration of input lines from the
            FINI manifest.
        label: A GN label that will be applied to all generated
            entries in the resulting list.
    Returns:
        An Entry list.
    """
    result = []
    for line in fini_manifest_lines:
        dst, _, src = line.strip().partition('=')
        entry = Entry(destination=dst, source=src, label=label)
        result.append(entry)

    return result


def _rewrite_elf_needed(dep: str) -> Optional[str]:
    """Rewrite an ELF DT_NEEDED dependency name.

  Args:
    dep: dependency name as it appears in ELF DT_NEEDED entry (e.g. 'libc.so')
  Returns:
    None if the dependency should be ignored, or the input dependency name,
    possibly rewritten for specific cases (e.g. 'libc.so' -> 'ld.so.1')
  """
    if dep == 'libzircon.so':
        # libzircon.so being injected by the kernel into user processes, it should
        # not appear in Fuchsia packages, and thus should be ignored.
        return None
    if dep == 'libc.so':
        # ld.so.1 acts as both the dynamic loader and C library, so any reference
        # to libc.so should be rewritten as 'ld.so.1'
        return 'ld.so.1'

    # For all other cases, just return the unmodified dependency name.
    return dep


def verify_elf_dependencies(
    binary_name: str,
    lib_dir: str,
    deps: Iterable[str],
    get_lib_dependencies: Callable[[str], Optional[List[str]]],
    visited_libraries: Set[str] = set()
) -> List[str]:
    """Verify the ELF dependencies of a given ELF binary.

  Args:
    binary_name: Name of the binary being verified, only used for error messages.
    lib_dir: The directory where the dependency libraries are supposed to be
      at runtime.
    deps: The list of DT_NEEDED dependency names for the current binary.
    get_lib_dependencies: A function that takes a runtime library path
      (e.g. "lib/libfoo.so") and returns the corresponding list of DT_NEEDED
      dependencies for its input, as a list of strings.
    visited_libraries: An optional set of file paths, which is updated
      by this function with the paths of the dependency libraries
      visited by this function.

  Returns:
    A list of error strings, which will be empty in case of success.
  """
    # Note that we do allow circular dependencies because they do happen
    # in practice. In particular when generating instrumented binaries,
    # e.g. for the 'asan' case (omitting libzircon.so):
    #
    #     libc.so (a.k.a. ld.so.1)
    #       ^     ^         ^ |
    #       |     |         | v
    #       |     |    libclang_rt.asan.so
    #       |     |     | ^      ^
    #       |     |     v |      |
    #       |    libc++abi.so    |
    #       |     |              |
    #       |     v              |
    #     libunwind.so-----------'
    #
    errors: List[str] = []
    queue = set(deps)
    while queue:
        dep = queue.pop()
        dep2 = _rewrite_elf_needed(dep)
        if dep2 is None:
            continue
        dep_path = os.path.join(lib_dir, dep2)
        if dep_path in visited_libraries:
            continue
        subdeps = get_lib_dependencies(dep_path)
        if subdeps is None:
            errors.append('%s missing dependency %s' % (binary_name, dep_path))
        else:
            visited_libraries.add(dep_path)
            for subdep in subdeps:
                if os.path.join(lib_dir, subdep) not in visited_libraries:
                    queue.add(subdep)

    return errors
