#!/usr/bin/env python3.8
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Tests for rustc_remote_wrapper."""

import rustc_remote_wrapper
import parameterized
import unittest

from parameterized import parameterized
from unittest import mock


class MainArgParseTests(unittest.TestCase):

    def testDefaults(self):
        args, forward = rustc_remote_wrapper.parse_main_args([])
        self.assertIsNone(args.help)
        self.assertFalse(args.dry_run)
        self.assertFalse(args.local)
        self.assertFalse(args.verbose)
        self.assertFalse(args.fsatrace)
        self.assertEqual(args.command, [])
        self.assertEqual(forward, [])

    @parameterized.expand(
        [
            (['-h'],),
            (['--help'],),
            (['--help', '--local'],),
            (['--local', '--help'],),
            (['--local', '--help', '--', 'echo', 'ekko'],),
        ])
    def testHelp(self, flags):
        args, forward = rustc_remote_wrapper.parse_main_args(flags)
        self.assertIsNotNone(args.help)
        # don't care about other fields

    def testDefaultOpposites(self):
        args, forward = rustc_remote_wrapper.parse_main_args(
            [
                '--dry-run', '--local', '--verbose', '--fsatrace', '--',
                'rustc', 'src/lib.rs'
            ])
        self.assertIsNone(args.help)
        self.assertTrue(args.dry_run)
        self.assertTrue(args.local)
        self.assertTrue(args.verbose)
        self.assertTrue(args.fsatrace)
        self.assertEqual(args.command, ['rustc', 'src/lib.rs'])
        self.assertEqual(forward, [])

    def testForwardRewrapperArgs(self):
        args, forward = rustc_remote_wrapper.parse_main_args(
            [
                '--local', '--forward-me', 'arg1', '--forward-me=too',
                '--verbose', '--', 'rustc', 'src/lib.rs'
            ])
        self.assertIsNone(args.help)
        self.assertTrue(args.local)
        self.assertTrue(args.verbose)
        self.assertEqual(forward, ['--forward-me', 'arg1', '--forward-me=too'])
        self.assertEqual(args.command, ['rustc', 'src/lib.rs'])


class CommandArgParseTests(unittest.TestCase):

    def testDefaults(self):
        args, filtered_command = rustc_remote_wrapper.parse_compile_command([])
        self.assertFalse(args.remote_disable)
        self.assertEqual(args.remote_inputs, [])
        self.assertEqual(args.remote_outputs, [])
        self.assertEqual(args.remote_flags, [])
        self.assertEqual(filtered_command, [])

    def testNormalCommand(self):
        args, filtered_command = rustc_remote_wrapper.parse_compile_command(
            ['echo', 'hello'])
        self.assertFalse(args.remote_disable)
        self.assertEqual(args.remote_inputs, [])
        self.assertEqual(args.remote_outputs, [])
        self.assertEqual(args.remote_flags, [])
        self.assertEqual(filtered_command, ['echo', 'hello'])

    def testRemoteDisable(self):
        args, filtered_command = rustc_remote_wrapper.parse_compile_command(
            ['--remote-disable'])
        self.assertTrue(args.remote_disable)
        self.assertEqual(filtered_command, [])

    def testRemoteInputs(self):
        args, filtered_command = rustc_remote_wrapper.parse_compile_command(
            ['--remote-inputs', 'aaa,bbb'])
        self.assertEqual(args.remote_inputs, 'aaa,bbb')
        self.assertEqual(filtered_command, [])

        args, filtered_command = rustc_remote_wrapper.parse_compile_command(
            ['--remote-inputs=aaa,bbb'])
        self.assertEqual(args.remote_inputs, 'aaa,bbb')
        self.assertEqual(filtered_command, [])

        args, filtered_command = rustc_remote_wrapper.parse_compile_command(
            ['--remote-inputs', 'aaa', '--remote-inputs', 'bbb'])
        self.assertEqual(args.remote_inputs, 'bbb')
        self.assertEqual(filtered_command, [])

    def testRemoteOutputs(self):
        args, filtered_command = rustc_remote_wrapper.parse_compile_command(
            ['--remote-outputs', 'xxx,yyy'])
        self.assertEqual(args.remote_outputs, 'xxx,yyy')
        self.assertEqual(filtered_command, [])

        args, filtered_command = rustc_remote_wrapper.parse_compile_command(
            ['--remote-outputs=xxx,yyy'])
        self.assertEqual(args.remote_outputs, 'xxx,yyy')
        self.assertEqual(filtered_command, [])

        args, filtered_command = rustc_remote_wrapper.parse_compile_command(
            ['--remote-outputs', 'xxx', '--remote-outputs', 'yyy'])
        self.assertEqual(args.remote_outputs, 'yyy')
        self.assertEqual(filtered_command, [])

    def testRemoteFlag(self):
        # argparse does not handle this:
        # args = rustc_remote_wrapper.parse_compile_command(['--remote-flag', '--some-rewrapper-flag=foobar'])
        # self.assertEqual(args.remote_flags, ['--some-rewrapper-flag=foobar'])

        args, filtered_command = rustc_remote_wrapper.parse_compile_command(
            ['--remote-flag=--some-rewrapper-flag=foobar'])
        self.assertEqual(args.remote_flags, ['--some-rewrapper-flag=foobar'])
        self.assertEqual(filtered_command, [])

        args, filtered_command = rustc_remote_wrapper.parse_compile_command(
            ['--remote-flag=--some-rewrapper-flag=foobar', '--remote-flag=baz'])
        self.assertEqual(
            args.remote_flags, ['--some-rewrapper-flag=foobar', 'baz'])
        self.assertEqual(filtered_command, [])


class RemoteCommandPseudoFlagsTests(unittest.TestCase):

    @parameterized.expand(
        [
            # (input, expected)
            (
                [],
                [],
            ),
            (
                ['foo', 'bar'],
                ['foo', 'bar'],
            ),
            (
                ['--remote-disable', 'foo', 'bar'],
                ['foo', 'bar'],
            ),
            (
                ['foo', '--remote-inputs=bar'],
                ['foo'],
            ),
            (
                ['--remote-outputs=foo', 'bar'],
                ['bar'],
            ),
            (
                ['foo', '--remote-flag=--bar', 'baz'],
                ['foo', 'baz'],
            ),
        ])
    def testCommandFiltering(self, input_command, expected_filtered_command):
        self.assertEqual(
            list(
                rustc_remote_wrapper.remove_command_pseudo_flags(
                    input_command)), expected_filtered_command)


class ApplyRemoteFlagsFromPseudoFlagsTests(unittest.TestCase):

    @parameterized.expand(
        [
            # (full command, expected value of .local)
            ([], False),
            (['--'], False),
            (['--local', '--'], True),
            (['--', '--remote-disable'], True),
        ])
    def testDefault(self, input_command, expected_local):
        main_config, rewrapper_opts = rustc_remote_wrapper.parse_main_args(
            input_command)
        command_params, filtered_command = rustc_remote_wrapper.parse_compile_command(
            main_config.command)
        rustc_remote_wrapper.apply_remote_flags_from_pseudo_flags(
            main_config, command_params)
        self.assertEqual(main_config.local, expected_local)


if __name__ == '__main__':
    unittest.main()
