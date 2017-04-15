// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Code generated by "stringer -type=Signature"; DO NOT EDIT.

package mbr

import "fmt"

const _Signature_name = "GPTSignature"

var _Signature_index = [...]uint8{0, 12}

func (i Signature) String() string {
	i -= 43605
	if i >= Signature(len(_Signature_index)-1) {
		return fmt.Sprintf("Signature(%d)", i+43605)
	}
	return _Signature_name[_Signature_index[i]:_Signature_index[i+1]]
}
