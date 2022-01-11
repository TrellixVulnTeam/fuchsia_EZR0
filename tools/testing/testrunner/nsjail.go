// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"errors"
	"fmt"
)

// MountPt describes the source, destination, and permissions for a
// mount point. This currently only supports bind mounts.
type MountPt struct {
	// Src is the path on the root filesystem to mount.
	Src string
	// Dst is the path inside the NsJail. If empty, we mount at Src.
	Dst string
	// Writable sets the mount points to rw (default is readonly).
	Writable bool
}

// NsJailCmdBuilder facilitates the construction of an NsJail command line
// invocation. See https://github.com/google/nsjail for NsJail docs.
type NsJailCmdBuilder struct {
	// Bin is the path to the NsJail binary. This is a required parameter.
	Bin string
	// Chroot is the path to a directory to set as the chroot.
	Chroot string
	// Cwd is the path to a directory to set as the working directory.
	// Note that this does not add any required mount points, the caller
	// is responsible for ensuring that the directory exists in the jail.
	Cwd string
	// IsolateNetwork indicates whether we should use a network namespace.
	IsolateNetwork bool
	// MountPoints is a list of locations on the current filesystem that
	// should be mounted into the NsJail.
	MountPoints []*MountPt
}

// addDefaultMounts adds mounts used by all processes.
// This is effectively an allowlist of mounts used by existing tests.
// Adding to this should be avoided if possible.
func (n *NsJailCmdBuilder) addDefaultMounts() {
	n.MountPoints = append(n.MountPoints, []*MountPt{
		// Many host tests run emulators, which requires KVM.
		{
			Src: "/dev/kvm",
		},
		// Many host tests rely on bash.
		{
			Src: "/bin/bash",
		},
		// /bin/bash, in turn, is dynamically linked and requires that we mount the
		// system linker.
		{
			Src: "/lib",
		},
		{
			Src: "/lib64",
		},
		// /usr/bin/dirname is used by the fctui_unittests.
		{
			Src: "/usr/bin/dirname",
		},
		// Additional mounts for convenience.
		{
			Src: "/dev/urandom",
		},
		{
			Src:      "/dev/null",
			Writable: true,
		},
	}...)
}

// Build takes the given subcmd and wraps it in the appropriate NsJail invocation.
func (n *NsJailCmdBuilder) Build(subcmd []string) ([]string, error) {
	// Validate the command.
	if n.Bin == "" {
		return nil, errors.New("NsJailCmdBuilder: Bin cannot be empty")
	} else if len(subcmd) == 0 {
		return nil, errors.New("NsJailCmdBuilder: subcmd cannot be empty")
	}

	// Add the default mounts.
	n.addDefaultMounts()

	// Build the actual command invocation.
	cmd := []string{n.Bin, "--keep_env"}
	if !n.IsolateNetwork {
		cmd = append(cmd, "--disable_clone_newnet")
	}
	if n.Chroot != "" {
		cmd = append(cmd, "--chroot", n.Chroot)
	}
	if n.Cwd != "" {
		cmd = append(cmd, "--cwd", n.Cwd)
	}
	for _, mountPt := range n.MountPoints {
		if mountPt.Src == "" {
			return nil, errors.New("NsJailCmdBuilder: Src cannot be empty in a mount point")
		}
		if mountPt.Dst == "" {
			mountPt.Dst = mountPt.Src
		}
		if mountPt.Writable {
			cmd = append(cmd, "--bindmount", fmt.Sprintf("%s:%s", mountPt.Src, mountPt.Dst))
		} else {
			cmd = append(cmd, "--bindmount_ro", fmt.Sprintf("%s:%s", mountPt.Src, mountPt.Dst))

		}
	}
	cmd = append(cmd, "--")
	cmd = append(cmd, subcmd...)
	return cmd, nil
}
