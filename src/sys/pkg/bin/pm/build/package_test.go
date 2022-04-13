// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"bytes"
	"crypto/rand"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pkg"
	far "go.fuchsia.dev/fuchsia/src/sys/pkg/lib/far/go"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/lib/merkle"
)

const testABIRevision uint64 = 0xE9CACD17EA11859D

// Verifies package manifests can be properly loaded.
func TestLoadPackageManifest(t *testing.T) {
	testCases := []struct {
		name               string
		buildDirContents   map[string]string
		manifestPathToLoad string
		expectedManifest   PackageManifest
		wantError          bool
	}{
		{
			name: "success valid path and blobs",
			buildDirContents: map[string]string{
				"package_manifest.json": `{
					"version": "1",
					"blobs": [
						{ "merkle": "0000000000000000000000000000000000000000000000000000000000000000" }
					]
				}`,
			},
			manifestPathToLoad: "package_manifest.json",
			expectedManifest: PackageManifest{
				Version: "1",
				Blobs: []PackageBlobInfo{
					{Merkle: MustDecodeMerkleRoot("0000000000000000000000000000000000000000000000000000000000000000")},
				},
			},
		},
		{
			name: "failure incompatible version",
			buildDirContents: map[string]string{
				"package_manifest.json": `{
					"version": "2",
				}`,
			},
			manifestPathToLoad: "package_manifest.json",
			wantError:          true,
		},
		{
			name: "failure json improperly formatted",
			buildDirContents: map[string]string{
				"package_manifest.json": `{
					Oops. This is not valid json.
				}`,
			},
			manifestPathToLoad: "package_manifest.json",
			wantError:          true,
		}, {
			name:               "failure package manifest does not exist",
			manifestPathToLoad: "non_existent_manifest.json",
			wantError:          true,
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			// Generate test env based on input.
			tempDirPath := createBuildDir(t, tc.buildDirContents)

			// Now that we're set up, we can actually load the package manifest.
			actualManifest, err := LoadPackageManifest(filepath.Join(tempDirPath, tc.manifestPathToLoad))

			// Ensure the results match the expectations.
			if (err == nil) == tc.wantError {
				t.Fatalf("got error [%v], want error? %t", err, tc.wantError)
			}
			if diff := cmp.Diff(actualManifest, &tc.expectedManifest); err == nil && diff != "" {
				t.Fatalf("got manifest %#v, expected %#v", actualManifest, tc.expectedManifest)
			}
		})
	}
}

func createBuildDir(t *testing.T, files map[string]string) string {
	dir := t.TempDir()
	for filename, data := range files {
		if err := os.MkdirAll(filepath.Join(dir, filepath.Dir(filename)), 0o700); err != nil {
			t.Fatal(err)
		}
		if err := ioutil.WriteFile(filepath.Join(dir, filename), []byte(data), 0o600); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

func TestInit(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	if err := Init(cfg); err != nil {
		t.Fatal(err)
	}

	f, err := os.Open(filepath.Join(cfg.OutputDir, "meta", "package"))
	if err != nil {
		t.Fatal(err)
	}
	var p pkg.Package
	if err := json.NewDecoder(f).Decode(&p); err != nil {
		t.Fatal(err)
	}
	f.Close()

	if want := cfg.PkgName; p.Name != want {
		t.Errorf("got %q, want %q", p.Name, want)
	}

	if want := "0"; p.Version != want {
		t.Errorf("got %q, want %q", p.Version, want)
	}
	os.RemoveAll(filepath.Dir(cfg.TempDir))

	// test condition where package name is empty, name should match the
	// name of the output directory
	cfg.PkgName = ""
	if err = Init(cfg); err != nil {
		t.Fatal(err)
	}

	f, err = os.Open(filepath.Join(cfg.OutputDir, "meta", "package"))
	if err != nil {
		t.Fatal(err)
	}

	if err = json.NewDecoder(f).Decode(&p); err != nil {
		t.Fatal(err)
	}
	f.Close()

	if want := filepath.Base(cfg.OutputDir); p.Name != want {
		t.Errorf("got %q, want %q", p.Name, want)
	}
}

func TestUpdate(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	TestPackage(cfg)

	manifest, err := cfg.Manifest()
	if err != nil {
		t.Fatal(err)
	}

	for _, f := range manifest.Content() {
		fd, err := os.Create(f)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := io.CopyN(fd, rand.Reader, 1024); err != nil {
			t.Fatal(err)
		}
		fd.Close()
	}

	if _, ok := manifest.Meta()["meta/contents"]; ok {
		t.Fatalf("unexpected pre-existing meta/contents in manifest")
	}

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}

	// FIXME(http://fxbug.dev/87309): In order to ease migration, initially the
	// abi-revision is not required.
	if _, ok := manifest.Meta()[abiRevisionKey]; ok {
		t.Fatalf("%s should not be created by default", abiRevisionKey)
	}

	contentsPath, ok := manifest.Meta()["meta/contents"]
	if !ok {
		t.Fatalf("meta/contents was not found in manifest after update")
	}

	f, err := os.Open(contentsPath)
	if err != nil {
		t.Fatal(err)
	}
	buf, err := ioutil.ReadAll(f)
	f.Close()
	if err != nil {
		t.Fatal(err)
	}

	lines := strings.Split(string(buf), "\n")
	sort.Strings(lines)
	if lines[0] == "" {
		lines = lines[1:]
	}

	contentFiles := []string{}
	for _, s := range TestFiles {
		if strings.HasPrefix(s, "meta/") {
			continue
		}
		contentFiles = append(contentFiles, s)
	}
	if len(lines) != len(contentFiles) {
		t.Fatalf("content lines mismatch: %v\n%v", lines, contentFiles)
	}

	files := []string{}
	files = append(files, contentFiles...)
	sort.Strings(files)
	for i, pf := range files {
		var tree merkle.Tree
		f, err := os.Open(manifest.Paths[pf])
		if err != nil {
			t.Fatal(err)
		}
		if _, err := tree.ReadFrom(f); err != nil {
			t.Fatal(err)
		}
		f.Close()
		want := fmt.Sprintf("%s=%x", pf, tree.Root())
		if lines[i] != want {
			t.Errorf("contents mismatch: got %q, want %q", lines[i], want)
			break
		}
	}
}

func TestUpdateTakesABIRevisionAndWritesABIRevision(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	cfg.PkgABIRevision = testABIRevision

	TestPackage(cfg)

	manifest, err := cfg.Manifest()
	if err != nil {
		t.Fatal(err)
	}

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}

	// FIXME(http://fxbug.dev/87309): In order to ease migration, initially the
	// abi-revision is not required.
	abiRevisionPath, ok := manifest.Meta()[abiRevisionKey]
	if !ok {
		t.Fatalf("%s was not found in manifest after update", abiRevisionKey)
	}

	f, err := os.Open(abiRevisionPath)
	if err != nil {
		t.Fatal(err)
	}

	var abiRevision uint64
	if err := binary.Read(f, binary.LittleEndian, &abiRevision); err != nil {
		t.Fatal(err)
	}
	if abiRevision != testABIRevision {
		t.Fatalf("expected ABI revision to be %x, not %x", testABIRevision, abiRevision)
	}
}

func TestValidate(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	TestPackage(cfg)

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}

	err := Validate(cfg)
	if err != nil {
		t.Fatalf("Unexpected validation error")
	}

	manifest, err := cfg.Manifest()
	if err != nil {
		t.Fatal(err)
	}

	for _, f := range RequiredFiles {
		manifest.Paths[f+"a"] = manifest.Paths[f]
		delete(manifest.Paths, f)
		if err := Validate(cfg); err == nil {
			t.Errorf("expected a validation error when %q is missing", f)
		}
		manifest.Paths[f] = manifest.Paths[f+"a"]
		delete(manifest.Paths, f+"a")
	}
}

func TestSeal(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	TestPackage(cfg)

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}

	if _, err := Seal(cfg); err != nil {
		t.Fatal(err)
	}

	// TODO(raggi): until we have far reader support this test only verifies that
	// the package meta data was correctly updated, it doesn't actually verify
	// that the contents of the far are correct.
	metafar := filepath.Join(cfg.OutputDir, "meta.far")
	if _, err := os.Stat(metafar); err != nil {
		t.Errorf("meta.far was not created")
	}

	merklefile := filepath.Join(cfg.OutputDir, "meta.far.merkle")
	if _, err := os.Stat(merklefile); err != nil {
		t.Errorf("meta.far.merkle was not created")
	}

	m, err := ioutil.ReadFile(merklefile)
	if err != nil {
		t.Fatal(err)
	}
	fromFile := string(m)

	var tree merkle.Tree
	f, err := os.Open(metafar)
	if err != nil {
		t.Fatal(err)
	}
	tree.ReadFrom(f)
	fromFar := fmt.Sprintf("%x", tree.Root())

	if fromFile != fromFar {
		t.Errorf("meta.far.merkle != merkle(meta.far)\n%q\n%q", fromFile, fromFar)
	}

	// Make sure the meta.far doesn't contain the ABI revision by default.
	f.Seek(0, io.SeekStart)
	r, err := far.NewReader(f)
	if err != nil {
		t.Fatal(err)
	}

	// FIXME(http://fxbug.dev/87309): In order to ease migration, initially the
	// abi-revision is not required.
	_, err = r.Open("meta/fuchsia.pkg/abi-revision")
	if err == nil {
		t.Fatalf("expected meta/fuchsia.pkg/abi-revision to not be present in meta.far")
	}
	if !errors.Is(err, os.ErrNotExist) {
		t.Fatalf("expected error to be does not exist, not %s", err)
	}
}

func TestSealCreatesABIRevisionFile(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	cfg.PkgABIRevision = testABIRevision

	TestPackage(cfg)

	if err := Update(cfg); err != nil {
		t.Fatal(err)
	}

	if _, err := Seal(cfg); err != nil {
		t.Fatal(err)
	}

	// TODO(raggi): until we have far reader support this test only verifies that
	// the package meta data was correctly updated, it doesn't actually verify
	// that the contents of the far are correct.
	metafar := filepath.Join(cfg.OutputDir, "meta.far")
	f, err := os.Open(metafar)
	if err != nil {
		t.Fatal(err)
	}

	r, err := far.NewReader(f)
	if err != nil {
		t.Fatal(err)
	}

	abiBytes, err := r.ReadFile(abiRevisionKey)
	if err != nil {
		t.Fatal(err)
	}

	var abiRevision uint64
	if err := binary.Read(bytes.NewReader(abiBytes), binary.LittleEndian, &abiRevision); err != nil {
		t.Fatal(err)
	}
	if abiRevision != testABIRevision {
		t.Fatalf("expected ABI revision to be %x, not %x", testABIRevision, abiRevision)
	}
}

func TestSealValidatesInvalidPackageRepository(t *testing.T) {
	defer func() {
		if r := recover(); r == nil {
			t.Fatalf("Expected to fail on invalid repository")
		}
	}()

	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	cfg.PkgRepository = "x,y"
	BuildTestPackage(cfg)

	if _, err := Seal(cfg); err == nil {
		t.Fatalf("Expected invalid package repository to generate error.")
	}
}
