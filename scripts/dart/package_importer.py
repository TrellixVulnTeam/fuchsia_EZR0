#!/usr/bin/env python3.8
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function

import argparse
import json
import os
import paths
import re
import shutil
import subprocess
import sys
import tempfile

sys.path += [
    os.path.join(paths.FUCHSIA_ROOT, 'third_party', 'pyyaml', 'src', 'lib')
]
import yaml

LICENSE_FILES = ['LICENSE', 'LICENSE.txt']

IGNORED_EXTENSIONS = [
    'css', 'html', 'js', 'log', 'old', 'out', 'packages', 'snapshot', 'zip'
]

LOCAL_PACKAGES = {
    'build_integration':
        '//third_party/dart/pkg/build_integration',
    'flutter':
        '//third_party/dart-pkg/git/flutter/packages/flutter',
    'flutter_test':
        '//third_party/dart-pkg/git/flutter/packages/flutter_test',
    'flutter_web_plugins':
        '//third_party/dart-pkg/git/flutter/packages/flutter_web_plugins',
    'func':
        '//third_party/dart/third_party/pkg/func',
    'sky_engine':
        '//prebuilt/third_party/sky_engine',
    'testing':
        '//third_party/dart/pkg/testing',
    'typed_mock':
        '//third_party/dart/pkg/typed_mock',
}

FORBIDDEN_PACKAGES = ['mojo', 'mojo_services']

# This is to account for https://github.com/flutter/devtools/issues/1148
PACKAGES_WITH_NO_LIB = ['devtools']

# A list of package names that have directories that should not be included.
FORBIDDEN_DIRS = {
    'characters': [
        'third_party/Wikipedia'  # Invalid license
    ]
}


def parse_packages_file(dot_packages_path):
    """ parse the list of packages and paths in .packages file """
    packages = []
    with open(dot_packages_path, encoding='utf-8') as dot_packages:
        # The packages specification says both '\r' and '\n' are valid line
        # delimiters, which matches Python's 'universal newline' concept.
        # Packages specification: https://github.com/dart-lang/dart_enhancement_proposals/blob/HEAD/Accepted/0005%20-%20Package%20Specification/DEP-pkgspec.md
        contents = dot_packages.read()
        for line in contents.splitlines():
            if line.startswith('#'):
                continue
            delim = line.find(':')
            if delim == -1:
                continue
            name = line[:delim]
            path = line[delim + 1:-1]
            packages.append((name, path))
    return packages


def get_deps(package_name, parsed_yaml, dep_type):
    if dep_type in parsed_yaml and parsed_yaml[dep_type]:
        deps = parsed_yaml[dep_type]
        # This is to avoid circular dependencies. See fxbug.dev/40784.
        if package_name == 'built_value' and 'built_value_generator' in deps:
            del deps['built_value_generator']
        return deps
    else:
        return {}


def safe_parse_yaml(yaml_path):
    """ parses a pubspec file that may be malformed """
    # Some yaml files can be malformed and have an extra tab at the end
    # of a line. This causes the parser to fail so we strip all tabs off
    # the end of the lines.
    with open(yaml_path, encoding='utf-8') as yaml_file:
        yaml_data = []
        for line in yaml_file.readlines():
            yaml_data.append(line.rstrip('\t\n'))
        yaml_doc = "\n".join(yaml_data)

        parsed = yaml.safe_load(yaml_doc)
        if not parsed:
            raise Exception('Could not parse yaml file: %s' % yaml_file)

    return parsed


def parse_min_sdk_version_and_full_dependencies(yaml_path):
    """ parse the content of a pubspec.yaml """
    parsed = safe_parse_yaml(yaml_path)

    package_name = parsed['name']
    # If a format like sdk: '>=a.b' or sdk: 'a.b' is found, we'll use a.b.
    # If sdk is not specified (or 'any'), 2.8 is used.
    # In all other cases 2.0 is used.
    env_sdk = parsed.get('environment', {}).get('sdk', 'any')
    match = re.search(r"^(>=)?((0|[1-9]\d*)\.(0|[1-9]\d*))", env_sdk)
    if match:
        min_sdk_version = match.group(2)
    elif env_sdk == 'any':
        min_sdk_version = '2.8'
    else:
        min_sdk_version = '2.0'
    deps = get_deps(package_name, parsed, 'dependencies')
    dev_deps = get_deps(package_name, parsed, 'dev_dependencies')
    dep_overrides = get_deps(package_name, parsed, 'dependency_overrides')
    pinned_deps = get_deps(package_name, parsed, 'pinned_dependencies')
    return (
        package_name, min_sdk_version, deps, dev_deps, dep_overrides,
        pinned_deps)


def parse_min_sdk_and_dependencies(yaml_path):
    """ parse the min sdk version and dependency map out of a pubspec.yaml """
    _, min_sdk_version, deps, _, _, pinned_deps = parse_min_sdk_version_and_full_dependencies(
        yaml_path)
    return min_sdk_version, deps, pinned_deps


def write_build_file(
        build_gn_path, package_name, name_with_version, language_version, deps,
        dart_sources):
    """ writes BUILD.gn file for Dart package with dependencies """
    with open(build_gn_path, 'w', encoding='utf-8') as build_gn:
        build_gn.write(
            '''# This file is generated by package_importer.py for %s

import("//build/dart/dart_library.gni")

dart_library("%s") {
  package_name = "%s"

  language_version = "%s"

  disable_analysis = true

  deps = [
''' % (name_with_version, package_name, package_name, language_version))
        for dep in deps:
            if dep in LOCAL_PACKAGES:
                build_gn.write('    "%s",\n' % LOCAL_PACKAGES[dep])
            else:
                build_gn.write('    "//third_party/dart-pkg/pub/%s",\n' % dep)
        build_gn.write('''  ]

  sources = [
''')
        for source in sorted(dart_sources):
            build_gn.write('    "%s",\n' % source)
        build_gn.write('''  ]
}
''')


def read_package_versions(base):
    '''Scans the packages in a given directory.'''
    result = {}
    for (root, dirs, files) in os.walk(base):
        for dir in dirs:
            spec = os.path.join(root, dir, 'pubspec.yaml')
            if not os.path.exists(spec):
                continue
            data = safe_parse_yaml(spec)
            result[data['name']] = data['version']
        break
    return result


def generate_package_diff(old_packages, new_packages, changelog):
    '''Writes a changelog file with package version changes.'''
    old = set(old_packages.items())
    new = set(new_packages.items())
    changed_keys = set([k for (k, _) in (old | new) - (old & new)])
    if not changed_keys:
        return
    max_key_width = max(map(lambda k: len(k), changed_keys))
    with open(changelog, 'w', encoding='utf-8') as changelog_file:
        for key in sorted(changed_keys):
            old = old_packages.get(key, '<none>')
            new = new_packages.get(key, '<none>')
            changelog_file.write(
                '%s %s --> %s\n' %
                (key.rjust(max_key_width), old.rjust(10), new.ljust(10)))


def valid_package_path(package_name, source_dir):
    if package_name in PACKAGES_WITH_NO_LIB:
        parent_dir = os.path.normpath(os.path.join(source_dir, os.pardir))
        return os.path.exists(parent_dir)
    else:
        return os.path.exists(source_dir)


def main():
    parser = argparse.ArgumentParser('Import dart packages from pub')
    parser.add_argument(
        '--dart', required=True, help='Path to the dart executable')
    parser.add_argument(
        '--pubspecs',
        nargs='+',
        help='Paths to packages containing pubspec.yaml files')
    parser.add_argument(
        '--git-pubspecs',
        nargs='+',
        help=
        'A list of git pubspec locations formatted as DEP_NAME,GIT_URI,COMMIT_HASH,SUBDIR'
    )
    parser.add_argument(
        '--projects',
        nargs='+',
        help='Paths to projects containing dependency files')
    parser.add_argument(
        '--output', required=True, help='Path to the output directory')
    parser.add_argument(
        '--changelog', help='Path to the changelog file to write', default=None)
    parser.add_argument(
        '--debug', help='Turns on debugging mode', action='store_true')
    args = parser.parse_args()

    def debug_print(message):
        if args.debug:
            print(message)

    tempdir = tempfile.mkdtemp()
    debug_print('Working directory: ' + tempdir)
    try:
        importer_dir = os.path.join(tempdir, 'importer')
        os.mkdir(importer_dir)

        # Read the requested dependencies from the canonical packages.
        packages = {}
        additional_deps = {}
        debug_print('------------------------')
        debug_print('Development dependencies')
        debug_print('------------------------')
        for path in args.pubspecs:
            yaml_file = os.path.join(path, 'pubspec.yaml')
            package_name, _, _, dev_deps, _, _ = parse_min_sdk_version_and_full_dependencies(
                yaml_file)
            packages[package_name] = path
            additional_deps.update(dev_deps)
            debug_print('# From ' + yaml_file)
            for pair in sorted(dev_deps.items()):
                debug_print(' - %s: %s' % pair)

        # Generate a manifest containing all the dependencies we care about.
        dependencies = {package_name: 'any' for package_name in packages.keys()}
        for dep, version in additional_deps.items():
            if dep in packages:
                continue
            dependencies[dep] = version
        debug_print('-------------------------')
        debug_print('Manually-set dependencies')
        debug_print('-------------------------')
        pinned_deps = None
        for project in args.projects:
            yaml_file = os.path.join(project, 'dart_dependencies.yaml')
            _, project_deps, pinned = parse_min_sdk_and_dependencies(yaml_file)
            pinned_deps = pinned if pinned else pinned_deps
            debug_print('# From ' + yaml_file)
            for dep, version in sorted(project_deps.items()):
                dependencies[dep] = version
                debug_print(' - %s: %s' % (dep, version))

        overrides = {
            package_name: {
                'path': path,
            } for package_name, path in packages.items()
        }
        # Start populating git-based dependencies.
        if args.git_pubspecs:
            debug_print('-----------------------------')
            debug_print('Mannualy-set git dependencies')
            debug_print('-----------------------------')
            for git_pubspec in args.git_pubspecs:
                dep_name, git_url, git_ref, git_path = git_pubspec.split(',')
                dependencies[dep_name] = 'any'
                overrides[dep_name] = {
                    'git': {
                        'url': git_url,
                        'ref': git_ref,
                        'path': git_path,
                    }
                }
            debug_print(yaml.safe_dump(dependencies, default_flow_style=False))

        pubspec_filename = os.path.join(importer_dir, 'pubspec.yaml')
        with open(pubspec_filename, 'w', encoding='utf-8') as pubspec:
            yaml.safe_dump(
                {
                    'name': 'importer',
                    'dependencies': dependencies,
                    'dependency_overrides': overrides,
                    'environment': {
                        'sdk': '>=2.0.0 <3.0.0'
                    }
                },
                pubspec,
                default_flow_style=not args.debug)

        old_packages = read_package_versions(args.output)

        # Use pub to load the dependencies into a local cache.
        pub_cache_dir = os.path.join(tempdir, 'pub_cache')
        os.mkdir(pub_cache_dir)
        env = os.environ
        env['PUB_CACHE'] = pub_cache_dir
        pub_get = [args.dart, 'pub', 'get', '--legacy-packages-file']
        if args.debug:
            pub_get.append('-v')
        subprocess.check_call(pub_get, cwd=importer_dir, env=env)

        # Walk the cache and copy the packages we are interested in.
        if os.path.exists(args.output):
            for (root, dirs, files) in os.walk(args.output):
                for dir in dirs:
                    if dir != '.git' and pinned_deps and dir not in pinned_deps.keys(
                    ):
                        shutil.rmtree(os.path.join(root, dir))
                # Only process the root of the output tree.
                break

        pub_packages = parse_packages_file(
            os.path.join(importer_dir, '.packages'))
        package_config = {
            'configVersion': 2,
            'packages': [],
            'generator': os.path.basename(__file__)
        }
        for package in pub_packages:
            if package[0] in packages:
                # Skip canonical packages.
                continue
            if not package[1].startswith('file://'):
                continue
            source_dir = package[1][len('file://'):]
            package_name = package[0]
            if not valid_package_path(package_name, source_dir):
                continue
            if source_dir.find('pub.dartlang.org') == -1:
                print(
                    'Package %s not from dartlang (%s), ignoring' %
                    (package[0], source_dir))
                continue
            # Don't import packages that live canonically in the tree.
            if package_name in LOCAL_PACKAGES:
                continue
            if package_name in FORBIDDEN_PACKAGES:
                print(
                    'Warning: dependency on forbidden package %s' %
                    package_name)
                continue
            # We expect the .packages file to point to a directory called 'lib'
            # inside the overall package, which will contain the LICENSE file
            # and other potentially useful directories like 'bin'.
            source_base_dir = os.path.dirname(os.path.abspath(source_dir))
            name_with_version = os.path.basename(source_base_dir)
            has_license = any(
                os.path.exists(os.path.join(source_base_dir, file_name))
                for file_name in LICENSE_FILES)
            if not has_license:
                print(
                    'Could not find license file for %s, skipping' %
                    package_name)
                continue
            pubspec_path = os.path.join(source_base_dir, 'pubspec.yaml')
            deps = []
            min_sdk_version = '2.8'
            if os.path.exists(pubspec_path):
                min_sdk_version, deps, _ = parse_min_sdk_and_dependencies(
                    pubspec_path)
            dest_dir = os.path.join(args.output, package_name)
            dart_sources = []
            # Add all dart files in source_dir subdirectory into dart_sources
            for (path, dirs, files) in os.walk(source_dir):
                for f in files:
                    if f.endswith('.dart'):
                        dart_sources.append(
                            os.path.relpath(os.path.join(path, f), source_dir))
            if os.path.exists(dest_dir):
                shutil.rmtree(dest_dir)
            shutil.copytree(
                source_base_dir,
                dest_dir,
                ignore=shutil.ignore_patterns(
                    *('*.' + extension for extension in IGNORED_EXTENSIONS)))
            # We don't need the 'test' directory of packages we import as that
            # directory exists to test that package and some of our packages
            # have very heavy test directories, so nuke those.
            test_path = os.path.join(dest_dir, 'test')
            if os.path.exists(test_path):
                shutil.rmtree(test_path)

            # Check to see if this package has any forbidden directories.
            if package_name in FORBIDDEN_DIRS:
                for d in FORBIDDEN_DIRS[package_name]:
                    forbidden_dir = os.path.join(dest_dir, d)
                    if os.path.exists(forbidden_dir):
                        shutil.rmtree(forbidden_dir)

            write_build_file(
                os.path.join(dest_dir, 'BUILD.gn'), package_name,
                name_with_version, min_sdk_version, deps, dart_sources)
            # All pub packages are required to have a lib/ dir, so it's safe to
            # hard-code this value.
            package_config['packages'].append(
                {
                    'name': package_name,
                    'rootUri': './%s/' % package_name,
                    'packageUri': 'lib/',
                    'languageVersion': min_sdk_version
                })
        # args.output == '//third_party/dart-pkg/pub/', so we'll try to
        # serialize package_config to JSON by using json.dumps and write to
        # //third_party/dart-pkg/pub/package_config.json.
        with open(os.path.join(args.output, 'package_config.json'), 'w',
                  encoding='utf-8') as package_config_json:
            package_config_json.write(
                json.dumps(package_config, sort_keys=True, indent=2))
        if args.changelog:
            new_packages = read_package_versions(args.output)
            generate_package_diff(old_packages, new_packages, args.changelog)

    finally:
        if not args.debug:
            shutil.rmtree(tempdir)


if __name__ == '__main__':
    sys.exit(main())
