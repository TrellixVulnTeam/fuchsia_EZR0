#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import re
import sys

def usage():
  print 'Usage:'
  print '  magma_generic.cc.gen.py INPUT EXISTING OUTPUT [--debug]'
  print '    INPUT    json file containing the magma interface definition'
  print '    EXISTING cpp file implementing zero or more magma entrypoints'
  print '    OUTPUT   destination path for the cpp file to generate'
  print '    --debug  optional flag to generate debug prints for entrypoints'
  print '  Example: magma_generic.cc.gen.py magma.json magma.cc magma_generic.cc'
  print '  Generates generic "glue" magma exports that directly translate between'
  print '  magma commands and virtmagma structs. Does not generate implementations'
  print '  for entrypoints that already exist in EXISTING.'

# License string for the top of the file.
def license():
  ret = ''
  ret += '// Copyright 2019 The Fuchsia Authors. All rights reserved.\n'
  ret += '// Use of this source code is governed by a BSD-style license that can be\n'
  ret += '// found in the LICENSE file.\n'
  return ret

# Warning string about auto-generation
def codegen_warning():
  ret = ''
  ret += '// NOTE: DO NOT EDIT THIS FILE! It is generated automatically by:\n'
  ret += '//   //src/graphics/lib/magma/src/libmagma_linux/magma_generic.cc.gen.py\n'
  return ret

# Includes lists.
def includes():
  ret = ''
  ret += '#include "magma.h"\n'
  ret += '#include "src/graphics/lib/magma/include/virtio/virtio_magma.h"\n'
  ret += '#include "src/graphics/lib/magma/src/libmagma_linux/virtmagma_util.h"\n'
  return ret

# Extract the non-"magma_" portion of the name of an export
def get_name(export):
  return export['name'][len('magma_'):]

# Trim already-implemented exports from an existing file
def trim_exports(exports, file):
  contents = file.readlines()
  exports_out = []
  for export in exports:
    found = False
    pattern = r'.*\s+' + export['name'] + r'\s*\('
    for line in contents:
      if re.match(pattern, line[:-1]):
        found = True
        break
    if not found:
      exports_out += [export]
  return exports_out

# Generate the signature for an export
def generate_signature(export):
  ret = export['type'] + ' ' + export['name'] + '(\n'
  for argument in export['arguments']:
    ret += '    ' + argument['type'] + ' ' + argument['name'] + ',\n'
  ret = ret[:-2] + ')\n'
  return ret

# Provide the appropriate error return statement for an export
def error_return(export):
  if export['type'] == 'magma_status_t':
    return 'return MAGMA_STATUS_INTERNAL_ERROR'
  if export['type'] == 'void':
    return 'return'
  return 'return -1'

# Splits the arguments for an export into inputs and outputs
def split_arguments(export):
  inputs = []
  outputs = []
  for argument in export['arguments']:
    if argument['name'][-4:] == '_out':
      assert argument['type'][-1] == '*', 'output argument not a pointer'
      outputs += [ argument ]
    else:
      inputs += [ argument ]
  return inputs, outputs

# Generate code to copy export arguments into the ioctl request struct
def generate_copy_in(inputs):
  ret = ''
  for argument in inputs:
    name = argument['name']
    ret += '    request.' + name + ' = (__typeof(request.' + name + '))' + name + ';\n'
  return ret

# Generate code to copy ioctl response members into export output arguments
def generate_copy_out(outputs, returns):
  ret = ''
  for argument in outputs:
    name = argument['name']
    ret += '    *' + name + ' = (__typeof(*' + name + '))response.' + name + ';\n'
  if returns != 'void':
    ret += '    ' + returns + ' result_return = (__typeof(result_return))(response.result_return);\n'
  return ret

# Generate code to unwrap applicable input objects
def generate_unwrap(export, needs_connection):
  ret = ''
  have_fd = False
  for argument in export['arguments']:
    type = argument['type']
    name = argument['name']
    if name == 'file_descriptor':
      have_fd = True
    if type == 'magma_connection_t':
      if needs_connection:
        ret += '    auto _connection = ' + name + ';\n'
      ret += '    auto _' + name + '_wrapped = virtmagma_connection_t::Get(' + name + ');\n'
      ret += '    ' + name + ' = _' + name + '_wrapped->Object();\n'
      if not have_fd:
        ret += '    int32_t file_descriptor = _' + name + '_wrapped->Parent().first;\n'
        have_fd = True
    if argument['type'] == 'magma_buffer_t':
      ret += '    auto _' + name + '_wrapped = virtmagma_buffer_t::Get(' + name + ');\n'
      ret += '    ' + name + ' = _' + name + '_wrapped->Object();\n'
      if not have_fd:
        ret += '    auto _' + name + '_parent_wrapped = virtmagma_connection_t::Get(_' + name + '_wrapped->Parent());\n'
        ret += '    int32_t file_descriptor = _' + name + '_parent_wrapped->Parent().first;\n'
        have_fd = True
    if argument['type'] == 'magma_semaphore_t':
      ret += '    auto _' + name + '_wrapped = virtmagma_semaphore_t::Get(' + name + ');\n'
      ret += '    ' + name + ' = _' + name + '_wrapped->Object();\n'
      if not have_fd:
        ret += '    auto _' + name + '_parent_wrapped = virtmagma_connection_t::Get(_' + name + '_wrapped->Parent());\n'
        ret += '    int32_t file_descriptor = _' + name + '_parent_wrapped->Parent().first;\n'
        have_fd = True
    if type == 'magma_device_t':
      ret += '    auto _' + name + '_wrapped = virtmagma_device_t::Get(' + name + ');\n'
      ret += '    ' + name + ' = _' + name + '_wrapped->Object();\n'
      if not have_fd:
        ret += '    int32_t file_descriptor = _' + name + '_wrapped->Parent().fd();\n'
        have_fd = True
    sub = 'handle'
    if name[-len(sub):] == sub:
      ret += '    auto _' + name + '_wrapped = GlobalHandleTable().find(' + name + ');\n'
      ret += '    if (_' + name + '_wrapped == GlobalHandleTable().end())\n'
      ret += '        ' + error_return(export) + ';\n'
      ret += '    ' + name + ' = _' + name + '_wrapped->second->Object();\n'
      if not have_fd:
        ret += '    int32_t file_descriptor = _' + name + '_wrapped->second->Parent();\n'
        have_fd = True
    if type == 'magma_handle_t':
      if not have_fd:
        ret += '    int32_t file_descriptor = ' + name + ';\n'
        have_fd = True
  if not have_fd:
    sys.exit('error: could not retrieve virtio fd from export "' + export['name'] + '"')
  return ret

# Generate code to wrap applicable output objects
def generate_wrap(export):
  ret = ''
  needs_connection = False
  for argument in export['arguments']:
    type = argument['type']
    name = argument['name']
    if type == 'magma_connection_t*':
      ret += '    *' + name + ' = virtmagma_connection_t::Create(*' + name + ', virtmagma_fd_pair(file_descriptor))->Wrap();\n'
    if type == 'magma_buffer_t*':
      ret += '    *' + name + ' = virtmagma_buffer_t::Create(*' + name + ', _connection)->Wrap();\n'
      needs_connection = True
    if type == 'magma_semaphore_t*':
      ret += '    *' + name + ' = virtmagma_semaphore_t::Create(*' + name + ', _connection)->Wrap();\n'
      needs_connection = True
    if type == 'magma_device_t*':
      ret += '    *' + name + ' = virtmagma_device_t::Create(*' + name + ', file_descriptor)->Wrap();\n'
    sub = 'handle_out'
    if name[-len(sub):] == sub:
      ret += '    GlobalHandleTable()[*' + name + '] = virtmagma_handle_t::Create(*' + name + ', file_descriptor);\n'

  if export['type'] == 'magma_handle_t':
    ret += '    GlobalHandleTable()[result_return] = virtmagma_handle_t::Create(result_return, file_descriptor);\n'

  return ret, needs_connection

# Generate an implementation for an export
def generate_export(export, gen_debug_prints):
  name = get_name(export)
  inputs, outputs = split_arguments(export)
  err = error_return(export)
  ret = generate_signature(export)
  ret += '{\n'
  if gen_debug_prints:
    ret += '    printf("%s\\n", __PRETTY_FUNCTION__);\n'
    for argument in export['arguments']:
      ret +='    printf("' + argument['name'] + ' = %ld\\n", (uint64_t)' + argument['name'] + ');\n'
  wrap_code, needs_connection = generate_wrap(export)
  ret += generate_unwrap(export, needs_connection)
  ret += '    virtio_magma_' + name + '_ctrl_t request{};\n'
  ret += '    virtio_magma_' + name + '_resp_t response{};\n'
  ret += '    request.hdr.type = VIRTIO_MAGMA_CMD_' + name.upper() + ';\n'
  ret += generate_copy_in(inputs)
  ret += '    if (!virtmagma_send_command(file_descriptor, &request, sizeof(request), &response, sizeof(response)))\n'
  ret += '        ' + err + ';\n'
  ret += '    if (response.hdr.type != VIRTIO_MAGMA_RESP_' + name.upper() + ')\n'
  ret += '        ' + err + ';\n'
  ret += generate_copy_out(outputs, export['type'])
  ret += wrap_code
  if export['type'] != 'void':
    ret += '    return result_return;\n'
  ret += '}\n'
  return ret

def main():
  nargs = len(sys.argv)
  debug = False
  if (nargs < 4 or nargs > 5):
    usage()
    exit(-1)
  if (nargs == 5):
    if sys.argv[4] != '--debug':
      usage()
      exit(-1)
    debug = True
  with open(sys.argv[1], 'r') as file:
    with open(sys.argv[2], 'r') as existing:
      with open(sys.argv[3], 'w') as dest:
        magma = json.load(file)['magma-interface']
        exports = trim_exports(magma['exports'], existing)
        contents = license() + '\n'
        contents += codegen_warning() + '\n'
        contents += includes() + '\n'
        for export in exports:
          contents += generate_export(export, debug) + '\n'
        dest.write(contents)

if __name__ == '__main__':
  sys.exit(main())
