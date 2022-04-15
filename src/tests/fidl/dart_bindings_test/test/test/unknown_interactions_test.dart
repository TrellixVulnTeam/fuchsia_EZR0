// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// ignore_for_file: avoid_catches_without_on_clauses

import 'dart:async';
import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_async.dart';
import 'package:fidl_fidl_test_dartbindingstest/fidl_test.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

/// Matches on the TXID of message.
class HasTxid extends CustomMatcher {
  HasTxid(matcher) : super('Message with txid that is', 'txid', matcher);

  @override
  int featureValueOf(dynamic actual) =>
      (actual as Uint8List).buffer.asByteData().getUint32(0, Endian.little);
}

/// Matches on the rest of a message, excluding TXID.
class ExcludingTxid extends CustomMatcher {
  ExcludingTxid(matcher)
      : super('Message, excluding txid, that is', 'message', matcher);

  @override
  List<int> featureValueOf(dynamic actual) => (actual as List<int>).sublist(4);
}

/// Matches an UnknownEvent's ordinal.
class UnknownEventWithOrdinal extends CustomMatcher {
  UnknownEventWithOrdinal(matcher)
      : super('UnknownEvent with ordinal that is', 'ordinal', matcher);

  @override
  int featureValueOf(dynamic actual) => (actual as UnknownEvent).ordinal;
}

/// Combines the TXID of an original message with a message body (excluding
/// txid) for use replying to a message.
ByteData buildReplyWithTxid(Uint8List original, List<int> body) =>
    Uint8List.fromList(original.sublist(0, 4) + body).buffer.asByteData();

/// Convert a list of ints to ByteData.
ByteData toByteData(List<int> body) =>
    Uint8List.fromList(body).buffer.asByteData();

class TestProtocol extends UnknownInteractionsProtocol$TestBase {
  TestProtocol({
    Future<void> Function() strictOneWay,
    Future<void> Function() flexibleOneWay,
    Future<void> Function() strictTwoWay,
    Future<void> Function() strictTwoWayErr,
    Future<void> Function() flexibleTwoWay,
    Future<void> Function() flexibleTwoWayErr,
    Future<void> Function(int) unknownOneWay,
    Future<void> Function(int) unknownTwoWay,
    Stream<void> strictEvent,
    Stream<void> strictEventErr,
    Stream<void> flexibleEvent,
    Stream<void> flexibleEventErr,
  })  : _strictOneWay = strictOneWay,
        _flexibleOneWay = flexibleOneWay,
        _strictTwoWay = strictTwoWay,
        _strictTwoWayErr = strictTwoWayErr,
        _flexibleTwoWay = flexibleTwoWay,
        _flexibleTwoWayErr = flexibleTwoWayErr,
        _$unknownOneWay = unknownOneWay,
        _$unknownTwoWay = unknownTwoWay,
        _strictEvent = strictEvent ?? StreamController.broadcast().stream,
        _strictEventErr = strictEventErr ?? StreamController.broadcast().stream,
        _flexibleEvent = flexibleEvent ?? StreamController.broadcast().stream,
        _flexibleEventErr =
            flexibleEventErr ?? StreamController.broadcast().stream;

  final Future<void> Function() _strictOneWay;
  final Future<void> Function() _flexibleOneWay;
  final Future<void> Function() _strictTwoWay;
  final Future<void> Function() _strictTwoWayErr;
  final Future<void> Function() _flexibleTwoWay;
  final Future<void> Function() _flexibleTwoWayErr;
  final Future<void> Function(int) _$unknownOneWay;
  final Future<void> Function(int) _$unknownTwoWay;
  final Stream<void> _strictEvent;
  final Stream<void> _strictEventErr;
  final Stream<void> _flexibleEvent;
  final Stream<void> _flexibleEventErr;

  @override
  Future<void> strictOneWay() => (_strictOneWay ?? super.strictOneWay)();
  @override
  Future<void> flexibleOneWay() => (_flexibleOneWay ?? super.flexibleOneWay)();
  @override
  Future<void> strictTwoWay() => (_strictTwoWay ?? super.strictTwoWay)();
  @override
  Future<void> strictTwoWayErr() =>
      (_strictTwoWayErr ?? super.strictTwoWayErr)();
  @override
  Future<void> flexibleTwoWay() => (_flexibleTwoWay ?? super.flexibleTwoWay)();
  @override
  Future<void> flexibleTwoWayErr() =>
      (_flexibleTwoWayErr ?? super.flexibleTwoWayErr)();
  @override
  Future<void> $unknownOneWay(int ordinal) =>
      (_$unknownOneWay ?? super.$unknownOneWay)(ordinal);
  @override
  Future<void> $unknownTwoWay(int ordinal) =>
      (_$unknownTwoWay ?? super.$unknownTwoWay)(ordinal);
  @override
  Stream<void> get strictEvent => _strictEvent;
  @override
  Stream<void> get strictEventErr => _strictEventErr;
  @override
  Stream<void> get flexibleEvent => _flexibleEvent;
  @override
  Stream<void> get flexibleEventErr => _flexibleEventErr;
}

class TestAjarProtocol extends UnknownInteractionsAjarProtocol$TestBase {
  TestAjarProtocol({
    Future<void> Function(int) unknownOneWay,
  }) : _$unknownOneWay = unknownOneWay;

  final Future<void> Function(int) _$unknownOneWay;

  @override
  Future<void> $unknownOneWay(int ordinal) =>
      (_$unknownOneWay ?? super.$unknownOneWay)(ordinal);
}

void main() {
  print('unknown-interactions-test');
  group('unknown-interactions', () {
    group('client', () {
      group('one-way-calls', () {
        UnknownInteractionsProtocolProxy proxy;
        Channel server;
        setUp(() {
          proxy = UnknownInteractionsProtocolProxy();
          server = proxy.ctrl.request().passChannel();
        });

        test('strict', () async {
          await proxy.strictOneWay();
          final ReadResult result = server.queryAndRead();
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0xab, 0x4c, 0xcb, 0x27, 0x2e, 0xc8, 0x72, 0x4e, //
              ]));
        });
        test('flexible', () async {
          await proxy.flexibleOneWay();
          final ReadResult result = server.queryAndRead();
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0xac, 0xc1, 0x53, 0x05, 0xd5, 0xef, 0xda, 0x02, //
              ]));
        });
      });

      group('two-way-calls', () {
        UnknownInteractionsProtocolProxy proxy;
        ChannelReader serverEnd;
        Future<ReadResult> serverRead;
        setUp(() {
          proxy = UnknownInteractionsProtocolProxy();
          final Completer<ReadResult> serverReadCompleter = Completer();
          serverEnd = ChannelReader()
            ..onReadable = () {
              try {
                serverReadCompleter.complete(serverEnd.channel.queryAndRead());
              } catch (e, st) {
                serverReadCompleter.completeError(e, st);
              } finally {
                serverEnd
                  ..onReadable = null
                  ..onError = null;
              }
            }
            ..onError = (e) {
              serverReadCompleter.completeError(e);
              serverEnd
                ..onReadable = null
                ..onError = null;
            }
            ..bind(proxy.ctrl.request().passChannel());
          serverRead = serverReadCompleter.future;
        });

        test('strict', () async {
          final clientCall = proxy.strictTwoWay();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x00, 0x01, //
                  0x94, 0x6b, 0x5c, 0xa2, 0xa6, 0xd2, 0x69, 0x18, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x00, 0x01, //
            0x94, 0x6b, 0x5c, 0xa2, 0xa6, 0xd2, 0x69, 0x18, //
          ]));
          await clientCall;
        });
        test('strict-err success', () async {
          final clientCall = proxy.strictTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x00, 0x01, //
                  0x6a, 0xe0, 0x47, 0x4d, 0x4b, 0x9a, 0xb1, 0x5e, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x00, 0x01, //
            0x6a, 0xe0, 0x47, 0x4d, 0x4b, 0x9a, 0xb1, 0x5e, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
          ]));
          await clientCall;
        });
        test('strict-err transport-err', () async {
          final clientCall = proxy.strictTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x00, 0x01, //
                  0x6a, 0xe0, 0x47, 0x4d, 0x4b, 0x9a, 0xb1, 0x5e, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x00, 0x01, //
            0x6a, 0xe0, 0x47, 0x4d, 0x4b, 0x9a, 0xb1, 0x5e, //
            // Result union with transport_err ordinal
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(
              clientCall,
              throwsA(isA<FidlError>().having((e) => e.code, 'code',
                  equals(FidlErrorCode.fidlStrictUnionUnknownField))));
        });
        test('flexible success', () async {
          final clientCall = proxy.flexibleTwoWay();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x63, 0x34, 0x95, 0xf8, 0xa7, 0x6d, 0x5a, 0x49, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x63, 0x34, 0x95, 0xf8, 0xa7, 0x6d, 0x5a, 0x49, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
          ]));
          await clientCall;
        });
        test('flexible unknown-method', () async {
          final clientCall = proxy.flexibleTwoWay();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x63, 0x34, 0x95, 0xf8, 0xa7, 0x6d, 0x5a, 0x49, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x63, 0x34, 0x95, 0xf8, 0xa7, 0x6d, 0x5a, 0x49, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(clientCall, throwsA(isA<UnknownMethodException>()));
        });
        test('flexible other-transport-err', () async {
          final clientCall = proxy.flexibleTwoWay();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x63, 0x34, 0x95, 0xf8, 0xa7, 0x6d, 0x5a, 0x49, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x63, 0x34, 0x95, 0xf8, 0xa7, 0x6d, 0x5a, 0x49, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(
              clientCall,
              throwsA(isA<FidlUnrecognizedTransportErrorError>()
                  .having((e) => e.transportErr, 'transportErr', equals(-1))));
        });
        test('flexible err-variant', () async {
          final clientCall = proxy.flexibleTwoWay();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0x63, 0x34, 0x95, 0xf8, 0xa7, 0x6d, 0x5a, 0x49, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0x63, 0x34, 0x95, 0xf8, 0xa7, 0x6d, 0x5a, 0x49, //
            // Result union with err
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(
              clientCall,
              throwsA(isA<FidlError>().having((e) => e.code, 'code',
                  equals(FidlErrorCode.fidlStrictUnionUnknownField))));
        });
        test('flexible-err success', () async {
          final clientCall = proxy.flexibleTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
            // Result union with success envelope to satisfy client side:
            // ordinal  ---------------------------------|
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
          ]));
          await clientCall;
        });
        test('flexible-err unknown-method', () async {
          final clientCall = proxy.flexibleTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(clientCall, throwsA(isA<UnknownMethodException>()));
        });
        test('flexible-err other-transport-err', () async {
          final clientCall = proxy.flexibleTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
            // Result union with transport_err
            // ordinal  ---------------------------------|
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(
              clientCall,
              throwsA(isA<FidlUnrecognizedTransportErrorError>()
                  .having((e) => e.transportErr, 'transportErr', equals(-1))));
        });
        test('flexible-err error-variant', () async {
          final clientCall = proxy.flexibleTwoWayErr();
          final clientMessage = await serverRead;
          expect(clientMessage.handles, isEmpty);
          expect(
              clientMessage.bytesAsUint8List(),
              allOf([
                HasTxid(isNonZero),
                ExcludingTxid(equals([
                  0x02, 0x00, 0x80, 0x01, //
                  0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
                ])),
              ]));
          serverEnd.channel
              .write(buildReplyWithTxid(clientMessage.bytesAsUint8List(), [
            0x02, 0x00, 0x80, 0x01, //
            0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
            // Result union with err
            // ordinal  ---------------------------------|
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
            // inline value -----|  nhandles |  flags ---|
            0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
          ]));
          expect(
              clientCall,
              throwsA(isA<MethodException<int>>()
                  .having((e) => e.value, 'err', equals(256))));
        });
      });

      group('events', () {
        group('open-protocol', () {
          UnknownInteractionsProtocolProxy proxy;
          Channel server;
          setUp(() {
            proxy = UnknownInteractionsProtocolProxy();
            server = proxy.ctrl.request().passChannel();
          });

          test('strict', () async {
            expect(proxy.ctrl.state, equals(InterfaceState.bound));
            expect(
                proxy.ctrl.stateChanges, emits(equals(InterfaceState.closed)));
            expect(proxy.$unknownEvents.isEmpty, completion(isTrue));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('flexible', () async {
            expect(proxy.$unknownEvents,
                emits(UnknownEventWithOrdinal(equals(0xff10ff10ff10ff10))));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
        group('ajar-protocol', () {
          UnknownInteractionsAjarProtocolProxy proxy;
          Channel server;
          setUp(() {
            proxy = UnknownInteractionsAjarProtocolProxy();
            server = proxy.ctrl.request().passChannel();
          });

          test('strict', () async {
            expect(proxy.ctrl.state, equals(InterfaceState.bound));
            expect(
                proxy.ctrl.stateChanges, emits(equals(InterfaceState.closed)));
            expect(proxy.$unknownEvents.isEmpty, completion(isTrue));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('flexible', () async {
            expect(proxy.$unknownEvents,
                emits(UnknownEventWithOrdinal(equals(0xff10ff10ff10ff10))));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
        group('closed-protocol', () {
          UnknownInteractionsClosedProtocolProxy proxy;
          Channel server;
          setUp(() {
            proxy = UnknownInteractionsClosedProtocolProxy();
            server = proxy.ctrl.request().passChannel();
          });

          test('strict', () async {
            expect(proxy.ctrl.state, equals(InterfaceState.bound));
            expect(
                proxy.ctrl.stateChanges, emits(equals(InterfaceState.closed)));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('flexible', () async {
            expect(proxy.ctrl.state, equals(InterfaceState.bound));
            expect(
                proxy.ctrl.stateChanges, emits(equals(InterfaceState.closed)));
            server.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
      });
    });

    group('server', () {
      Future<ReadResult> read(Channel channel) {
        final Completer<ReadResult> readCompleter = Completer();
        ChannelReader reader = ChannelReader();
        reader
          ..onReadable = () {
            try {
              readCompleter.complete(reader.channel.queryAndRead());
            } catch (e, st) {
              readCompleter.completeError(e, st);
            } finally {
              reader
                ..onReadable = null
                ..onError = null;
            }
          }
          ..onError = (e) {
            readCompleter.completeError(e);
            reader
              ..onReadable = null
              ..onError = null;
          }
          ..bind(channel);
        return readCompleter.future;
      }

      group('one-way-calls', () {
        group('open-protocol', () {
          test('unknown-strict', () {
            final Completer<int> unknownOneWayCall = Completer();
            final TestProtocol server =
                TestProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final UnknownInteractionsProtocolBinding binding =
                UnknownInteractionsProtocolBinding();
            final Channel client = binding.wrap(server).passChannel();
            expect(unknownOneWayCall.future, doesNotComplete);
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () {
            final Completer<int> unknownOneWayCall = Completer();
            final TestProtocol server =
                TestProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(unknownOneWayCall.future,
                completion(equals(0xff10ff10ff10ff10)));
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
        group('ajar-protocol', () {
          test('unknown-strict', () {
            final Completer<int> unknownOneWayCall = Completer();
            final TestAjarProtocol server =
                TestAjarProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final UnknownInteractionsAjarProtocolBinding binding =
                UnknownInteractionsAjarProtocolBinding();
            final Channel client = binding.wrap(server).passChannel();
            expect(unknownOneWayCall.future, doesNotComplete);
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () {
            final Completer<int> unknownOneWayCall = Completer();
            final TestAjarProtocol server =
                TestAjarProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final Channel client = UnknownInteractionsAjarProtocolBinding()
                .wrap(server)
                .passChannel();
            expect(unknownOneWayCall.future,
                completion(equals(0xff10ff10ff10ff10)));
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
        group('closed-protocol', () {
          test('unknown-strict', () {
            final UnknownInteractionsClosedProtocolBinding binding =
                UnknownInteractionsClosedProtocolBinding();
            final Channel client = binding
                .wrap(UnknownInteractionsClosedProtocol$TestBase())
                .passChannel();
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () {
            final UnknownInteractionsClosedProtocolBinding binding =
                UnknownInteractionsClosedProtocolBinding();
            final Channel client = binding
                .wrap(UnknownInteractionsClosedProtocol$TestBase())
                .passChannel();
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
      });
      group('two-way-calls', () {
        group('open-protocol', () {
          test('known-strict', () async {
            final Completer<void> callReceived = Completer();
            final TestProtocol server = TestProtocol(strictTwoWay: () async {
              callReceived.complete();
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(callReceived.future, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x94, 0x6b, 0x5c, 0xa2, 0xa6, 0xd2, 0x69, 0x18, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                  0x94, 0x6b, 0x5c, 0xa2, 0xa6, 0xd2, 0x69, 0x18, //
                ]));
          });
          test('known-strict-err', () async {
            final Completer<void> callReceived = Completer();
            final TestProtocol server = TestProtocol(strictTwoWayErr: () async {
              callReceived.complete();
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(callReceived.future, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x6a, 0xe0, 0x47, 0x4d, 0x4b, 0x9a, 0xb1, 0x5e, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                  0x6a, 0xe0, 0x47, 0x4d, 0x4b, 0x9a, 0xb1, 0x5e, //
                  // Result union with success:
                  // ordinal  ---------------------------------|
                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                  // inline value -----|  nhandles |  flags ---|
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
                ]));
          });
          test('known-flexible', () async {
            final Completer<void> callReceived = Completer();
            final TestProtocol server = TestProtocol(flexibleTwoWay: () async {
              callReceived.complete();
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(callReceived.future, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x63, 0x34, 0x95, 0xf8, 0xa7, 0x6d, 0x5a, 0x49, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                  0x63, 0x34, 0x95, 0xf8, 0xa7, 0x6d, 0x5a, 0x49, //
                  // Result union with success:
                  // ordinal  ---------------------------------|
                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                  // inline value -----|  nhandles |  flags ---|
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
                ]));
          });
          test('known-flexible-err success', () async {
            final Completer<void> callReceived = Completer();
            final TestProtocol server =
                TestProtocol(flexibleTwoWayErr: () async {
              callReceived.complete();
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(callReceived.future, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                  0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
                  // Result union with success:
                  // ordinal  ---------------------------------|
                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                  // inline value -----|  nhandles |  flags ---|
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
                ]));
          });
          test('known-flexible-err error', () async {
            final Completer<void> callReceived = Completer();
            final TestProtocol server =
                TestProtocol(flexibleTwoWayErr: () async {
              callReceived.complete();
              throw MethodException(103);
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(callReceived.future, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                  0xe2, 0x05, 0x74, 0x50, 0x83, 0x20, 0x8f, 0x22, //
                  // Result union with err:
                  // ordinal  ---------------------------------|
                  0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                  // inline value -----|  nhandles |  flags ---|
                  0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
                ]));
          });
          test('unknown-strict', () {
            final Completer<int> unknownTwoWayCall = Completer();
            final TestProtocol server =
                TestProtocol(unknownTwoWay: (int ordinal) async {
              unknownTwoWayCall.complete(ordinal);
            });
            final UnknownInteractionsProtocolBinding binding =
                UnknownInteractionsProtocolBinding();
            final Channel client = binding.wrap(server).passChannel();
            expect(unknownTwoWayCall.future, doesNotComplete);
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () async {
            final Completer<int> unknownTwoWayCall = Completer();
            final TestProtocol server =
                TestProtocol(unknownTwoWay: (int ordinal) async {
              unknownTwoWayCall.complete(ordinal);
            });
            final Channel client =
                UnknownInteractionsProtocolBinding().wrap(server).passChannel();
            expect(unknownTwoWayCall.future,
                completion(equals(0xff10ff10ff10ff10)));
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
            final ReadResult result = await read(client);
            expect(result.handles, isEmpty);
            expect(
                result.bytesAsUint8List(),
                equals([
                  0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                  0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
                  // Result union with transport_err:
                  // ordinal  ---------------------------------|
                  0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                  // inline value -----|  nhandles |  flags ---|
                  0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, //
                ]));
          });
        });
        group('ajar-protocol', () {
          test('unknown-strict', () {
            final Completer<int> unknownOneWayCall = Completer();
            final TestAjarProtocol server =
                TestAjarProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final UnknownInteractionsAjarProtocolBinding binding =
                UnknownInteractionsAjarProtocolBinding();
            final Channel client = binding.wrap(server).passChannel();
            expect(unknownOneWayCall.future, doesNotComplete);
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () async {
            final Completer<int> unknownOneWayCall = Completer();
            final TestAjarProtocol server =
                TestAjarProtocol(unknownOneWay: (int ordinal) async {
              unknownOneWayCall.complete(ordinal);
            });
            final UnknownInteractionsAjarProtocolBinding binding =
                UnknownInteractionsAjarProtocolBinding();
            final Channel client = binding.wrap(server).passChannel();
            expect(unknownOneWayCall.future, doesNotComplete);
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
        group('closed-protocol', () {
          test('unknown-strict', () {
            final UnknownInteractionsClosedProtocolBinding binding =
                UnknownInteractionsClosedProtocolBinding();
            final Channel client = binding
                .wrap(UnknownInteractionsClosedProtocol$TestBase())
                .passChannel();
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
          test('unknown-flexible', () async {
            final UnknownInteractionsClosedProtocolBinding binding =
                UnknownInteractionsClosedProtocolBinding();
            final Channel client = binding
                .wrap(UnknownInteractionsClosedProtocol$TestBase())
                .passChannel();
            expect(binding.whenClosed, completes);
            client.write(toByteData([
              0xab, 0xcd, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
              0x10, 0xff, 0x10, 0xff, 0x10, 0xff, 0x10, 0xff, //
            ]));
          });
        });
      });
      group('events', () {
        test('strict', () async {
          final TestProtocol server =
              TestProtocol(strictEvent: Stream.fromFuture(Future.value()));
          final Channel client =
              UnknownInteractionsProtocolBinding().wrap(server).passChannel();
          final ReadResult result = await read(client);
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x08, 0xc8, 0x4b, 0x5d, 0x97, 0x8f, 0xe4, 0x02, //
              ]));
        });
        test('strict-err', () async {
          final TestProtocol server =
              TestProtocol(strictEventErr: Stream.fromFuture(Future.value()));
          final Channel client =
              UnknownInteractionsProtocolBinding().wrap(server).passChannel();
          final ReadResult result = await read(client);
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01, //
                0x65, 0x15, 0xfe, 0xb4, 0x19, 0xd5, 0x1f, 0x6b, //
                // Result union with success:
                // ordinal  ---------------------------------|
                0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                // inline value -----|  nhandles |  flags ---|
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
              ]));
        });
        test('flexible', () async {
          final TestProtocol server =
              TestProtocol(flexibleEvent: Stream.fromFuture(Future.value()));
          final Channel client =
              UnknownInteractionsProtocolBinding().wrap(server).passChannel();
          final ReadResult result = await read(client);
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0xa7, 0x73, 0x13, 0xdd, 0x0b, 0xc1, 0xdc, 0x29, //
              ]));
        });
        test('flexible-err success', () async {
          final TestProtocol server =
              TestProtocol(flexibleEventErr: Stream.fromFuture(Future.value()));
          final Channel client =
              UnknownInteractionsProtocolBinding().wrap(server).passChannel();
          final ReadResult result = await read(client);
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x5b, 0x2e, 0xcf, 0x79, 0xdf, 0xb3, 0xc0, 0x0d, //
                // Result union with success:
                // ordinal  ---------------------------------|
                0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                // inline value -----|  nhandles |  flags ---|
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
              ]));
        });
        test('flexible-err error', () async {
          final TestProtocol server = TestProtocol(
              flexibleEventErr:
                  Stream.fromFuture(Future.error(MethodException(256))));
          final Channel client =
              UnknownInteractionsProtocolBinding().wrap(server).passChannel();
          final ReadResult result = await read(client);
          expect(result.handles, isEmpty);
          expect(
              result.bytesAsUint8List(),
              equals([
                0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01, //
                0x5b, 0x2e, 0xcf, 0x79, 0xdf, 0xb3, 0xc0, 0x0d, //
                // Result union with error:
                // ordinal  ---------------------------------|
                0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
                // inline value -----|  nhandles |  flags ---|
                0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, //
              ]));
        });
      });
    });
  });
}
