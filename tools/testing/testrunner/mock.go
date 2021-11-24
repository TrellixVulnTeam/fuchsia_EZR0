// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"context"
	"io"
)

type MockFFXTester struct {
	CmdsCalled []string
}

func (f *MockFFXTester) SetStdoutStderr(_, _ io.Writer) {
}

func (f *MockFFXTester) run(cmd string) error {
	f.CmdsCalled = append(f.CmdsCalled, cmd)
	return nil
}

func (f *MockFFXTester) Test(_ context.Context, _ string, _ ...string) error {
	return f.run("test")
}

func (f *MockFFXTester) Snapshot(_ context.Context, _, _ string) error {
	return f.run("snapshot")
}

func (f *MockFFXTester) Stop() error {
	return f.run("stop")
}

func (f *MockFFXTester) ContainsCmd(cmd string) bool {
	for _, c := range f.CmdsCalled {
		if c == cmd {
			return true
		}
	}
	return false
}
