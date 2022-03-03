// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fidl/c_generator.h>
#include <fidl/experimental_flags.h>
#include <fidl/flat/compiler.h>
#include <fidl/flat_ast.h>
#include <fidl/json_generator.h>
#include <fidl/json_schema.h>
#include <fidl/lexer.h>
#include <fidl/names.h>
#include <fidl/ordinals.h>
#include <fidl/parser.h>
#include <fidl/source_manager.h>
#include <fidl/tables_generator.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

void Usage() {
  std::cout
      << "usage: fidlc [--tables TABLES_PATH]\n"
         "             [--json JSON_PATH]\n"
         "             [--name LIBRARY_NAME]\n"
         "             [--experimental FLAG_NAME]\n"
         "             [--werror]\n"
         "             [--format=[text|json]]\n"
         "             [--json-schema]\n"
         "             [--depfile DEPFILE_PATH]\n"
         "             [--files [FIDL_FILE...]...]\n"
         "             [--help]\n"
         "\n"
         " * `--tables TABLES_PATH`. If present, this flag instructs `fidlc` to output\n"
         "   coding tables at the given path. The coding tables are required to encode and\n"
         "   decode messages from the C and C++ bindings.\n"
         "\n"
         " * `--json JSON_PATH`. If present, this flag instructs `fidlc` to output the\n"
         "   library's intermediate representation at the given path. The intermediate\n"
         "   representation is JSON that conforms to the schema available via --json-schema.\n"
         "   The intermediate representation is used as input to the various backends.\n"
         "\n"
         " * `--convert-syntax CONVERTED_SYNTAX_PATH`. If present, this flag instructs `fidlc`\n"
         "   to output the last file listed with updated syntax at the given path. The input\n"
         "   file must be written in the old syntax for this to succeed.\n"
         "\n"
         " * `--name LIBRARY_NAME`. If present, this flag instructs `fidlc` to validate\n"
         "   that the library being compiled has the given name. This flag is useful to\n"
         "   cross-check between the library's declaration in a build system and the\n"
         "   actual contents of the library.\n"
         "\n"
         " * `--experimental FLAG_NAME`. If present, this flag enables an experimental\n"
         "    feature of fidlc.\n"
         "\n"
         " * `--depfile DEPFILE_PATH`. Path of depfile generated by `fidlc`. This depfile is\n"
         "   used to get correct incremental compilation rules. This file is populated by fidlc\n"
         "   as Line1: out1: in1 in2 in3, Line2: out2: in1 in2 in3 ... Where out[1-2] are all the\n"
         "   outputs generated by fidlc and in[1-3] are the files read. The input files are\n"
         "   what are passed by --files. Output files are those generated by fidlc.\n"
         "\n"
         " * `--files [FIDL_FILE...]...`. Each `--file [FIDL_FILE...]` chunk of arguments\n"
         "   describes a library, all of which must share the same top-level library name\n"
         "   declaration. Libraries must be presented in dependency order, with later\n"
         "   libraries able to use declarations from preceding libraries but not vice versa.\n"
         "   Output is only generated for the final library, not for each of its dependencies.\n"
         "\n"
         " * `--json-schema`. If present, this flag instructs `fidlc` to output the\n"
         "   JSON schema of the intermediate representation.\n"
         "\n"
         " * `--format=[text|json]`. If present, this flag sets the output mode of `fidlc`.\n"
         "    This specifies whether to output errors and warnings, if compilation fails, in\n"
         "    plain text (the default), or as JSON.\n"
         "\n"
         " * `--werror`. Treats warnings as errors.\n"
         "\n"
         " * `--help`. Prints this help, and exit immediately.\n"
         "\n"
         "All of the arguments can also be provided via a response file, denoted as\n"
         "`@responsefile`. The contents of the file at `responsefile` will be interpreted\n"
         "as a whitespace-delimited list of arguments. Response files cannot be nested.\n"
         "\n"
         "See <https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/compiler>\n"
         "for more information.\n";
  std::cout.flush();
}

void PrintJsonSchema() {
  std::cout << JsonSchema::schema() << "\n";
  std::cout.flush();
}

[[noreturn]] void FailWithUsage(const char* message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  Usage();
  exit(1);
}

[[noreturn]] void Fail(const char* message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  exit(1);
}

void MakeParentDirectory(const std::string& filename) {
  std::string::size_type slash = 0;

  for (;;) {
    slash = filename.find('/', slash);
    if (slash == filename.npos) {
      return;
    }

    std::string path = filename.substr(0, slash);
    ++slash;
    if (path.size() == 0u) {
      // Skip creating "/".
      continue;
    }

    if (mkdir(path.data(), 0755) != 0 && errno != EEXIST) {
      Fail("Could not create directory %s for output file %s: error %s\n", path.data(),
           filename.data(), strerror(errno));
    }
  }
}

std::fstream Open(std::string filename, std::ios::openmode mode) {
  if ((mode & std::ios::out) != 0) {
    MakeParentDirectory(filename);
  }

  std::fstream stream;
  stream.open(filename, mode);
  if (!stream.is_open()) {
    Fail("Could not open file: %s\n", filename.data());
  }
  return stream;
}

class Arguments {
 public:
  virtual ~Arguments() {}

  virtual std::string Claim() = 0;
  virtual bool Remaining() const = 0;
};

class ResponseFileArguments : public Arguments {
 public:
  ResponseFileArguments(std::string_view filename)
      : file_(Open(std::string(filename), std::ios::in)) {
    ConsumeWhitespace();
  }

  std::string Claim() override {
    std::string argument;
    while (Remaining() && !IsWhitespace()) {
      argument.push_back(static_cast<char>(file_.get()));
    }
    ConsumeWhitespace();
    return argument;
  }

  bool Remaining() const override { return !file_.eof(); }

 private:
  bool IsWhitespace() {
    switch (file_.peek()) {
      case ' ':
      case '\n':
      case '\r':
      case '\t':
        return true;
      default:
        return false;
    }
  }

  void ConsumeWhitespace() {
    while (Remaining() && IsWhitespace()) {
      file_.get();
    }
  }

  std::fstream file_;
};

class ArgvArguments : public Arguments {
 public:
  ArgvArguments(int count, char** arguments)
      : count_(count), arguments_(const_cast<const char**>(arguments)) {}

  std::string Claim() override {
    if (response_file_.get()) {
      if (response_file_->Remaining()) {
        return response_file_->Claim();
      }
      response_file_.reset();
    }
    if (count_ < 1) {
      FailWithUsage("Missing part of an argument\n");
    }
    std::string argument = arguments_[0];
    --count_;
    ++arguments_;
    if (argument.size() == 0 || argument[0] != '@') {
      return argument;
    }

    std::string_view rsp_file = argument.data() + 1;
    response_file_ = std::make_unique<ResponseFileArguments>(rsp_file);
    return Claim();
  }

  bool Remaining() const override {
    if (response_file_.get() && response_file_->Remaining())
      return true;

    return count_ > 0;
  }

 private:
  int count_;
  const char** arguments_;
  std::unique_ptr<ResponseFileArguments> response_file_;
};

enum struct Behavior {
  kCHeader,
  kCClient,
  kCServer,
  kTables,
  kJSON,
};

bool Parse(const fidl::SourceFile& source_file, fidl::Reporter* reporter,
           fidl::flat::Compiler* compiler, const fidl::ExperimentalFlags& experimental_flags) {
  fidl::Lexer lexer(source_file, reporter);
  fidl::Parser parser(&lexer, reporter, experimental_flags);
  auto ast = parser.Parse();
  if (!parser.Success()) {
    return false;
  }
  if (!compiler->ConsumeFile(std::move(ast))) {
    return false;
  }
  return true;
}

void Write(const std::ostringstream& output_stream, const std::string file_path) {
  std::string contents = output_stream.str();
  struct stat st;
  if (!stat(file_path.c_str(), &st)) {
    // File exists.
    size_t current_size = st.st_size;
    if (current_size == contents.size()) {
      // Lengths match.
      std::string current_contents(current_size, '\0');
      std::fstream current_file = Open(file_path, std::ios::in);
      current_file.read(current_contents.data(), current_size);
      if (current_contents == contents) {
        // Contents match, no need to write the file.
        return;
      }
    }
  }
  std::fstream file = Open(file_path, std::ios::out);
  file << output_stream.str();
  file.flush();
  if (file.fail()) {
    Fail("Failed to flush output to file: %s\n", file_path.c_str());
  }
}

}  // namespace

// TODO(pascallouis): remove forward declaration, this was only introduced to
// reduce diff size while breaking things up.
int compile(fidl::Reporter* reporter, std::string library_name, std::string dep_file_path,
            const std::vector<std::string>& source_list,
            const std::vector<std::pair<Behavior, std::string>>& outputs,
            const std::vector<fidl::SourceManager>& source_managers,
            fidl::ExperimentalFlags experimental_flags);

int main(int argc, char* argv[]) {
  auto args = std::make_unique<ArgvArguments>(argc, argv);

  // Parse the program name.
  args->Claim();
  if (!args->Remaining()) {
    Usage();
    exit(0);
  }

  std::string library_name;

  std::string dep_file_path;

  bool warnings_as_errors = false;
  std::string format = "text";
  std::vector<std::pair<Behavior, std::string>> outputs;
  fidl::ExperimentalFlags experimental_flags;
  while (args->Remaining()) {
    // Try to parse an output type.
    std::string behavior_argument = args->Claim();
    if (behavior_argument == "--help") {
      Usage();
      exit(0);
    } else if (behavior_argument == "--json-schema") {
      PrintJsonSchema();
      exit(0);
    } else if (behavior_argument == "--werror") {
      warnings_as_errors = true;
    } else if (behavior_argument.rfind("--format") == 0) {
      const auto equals = behavior_argument.rfind("=");
      if (equals == std::string::npos) {
        FailWithUsage("Unknown value for flag `format`\n");
      }
      const auto format_value = behavior_argument.substr(equals + 1, behavior_argument.length());
      if (format_value != "text" && format_value != "json") {
        FailWithUsage("Unknown value `%s` for flag `format`\n", format_value.data());
      }
      format = format_value;
    } else if (behavior_argument == "--deprecated-fuchsia-only-c-header") {
      std::string path = args->Claim();
      outputs.emplace_back(std::make_pair(Behavior::kCHeader, path));
    } else if (behavior_argument == "--deprecated-fuchsia-only-c-client") {
      std::string path = args->Claim();
      outputs.emplace_back(std::make_pair(Behavior::kCClient, path));
    } else if (behavior_argument == "--deprecated-fuchsia-only-c-server") {
      std::string path = args->Claim();
      outputs.emplace_back(std::make_pair(Behavior::kCServer, path));
    } else if (behavior_argument == "--tables") {
      std::string path = args->Claim();
      outputs.emplace_back(std::make_pair(Behavior::kTables, path));
    } else if (behavior_argument == "--json") {
      std::string path = args->Claim();
      outputs.emplace_back(std::make_pair(Behavior::kJSON, path));
    } else if (behavior_argument == "--name") {
      library_name = args->Claim();
    } else if (behavior_argument == "--experimental") {
      std::string flag = args->Claim();
      if (!experimental_flags.SetFlagByName(flag)) {
        FailWithUsage("Unknown experimental flag %s\n", flag.data());
      }
    } else if (behavior_argument == "--depfile") {
      dep_file_path = args->Claim();
    } else if (behavior_argument == "--files") {
      // Start parsing filenames.
      break;
    } else {
      FailWithUsage("Unknown argument: %s\n", behavior_argument.data());
    }
  }

  // Prepare source files.
  std::vector<fidl::SourceManager> source_managers;
  std::vector<std::string> source_list;
  source_managers.push_back(fidl::SourceManager());
  while (args->Remaining()) {
    std::string arg = args->Claim();
    if (arg == "--files") {
      source_managers.emplace_back();
    } else {
      if (!source_managers.back().CreateSource(arg.data())) {
        Fail("Couldn't read in source data from %s\n", arg.data());
      }
      source_list.emplace_back(arg.data());
    }
  }

  // Ready. Set. Go.
  fidl::Reporter reporter;
  reporter.set_warnings_as_errors(warnings_as_errors);
  auto status = compile(&reporter, library_name, dep_file_path, source_list, outputs,
                        source_managers, experimental_flags);
  if (format == "json") {
    reporter.PrintReportsJson();
  } else {
    bool enable_color = !std::getenv("NO_COLOR") && isatty(fileno(stderr));
    reporter.PrintReports(enable_color);
  }
  return status;
}

int compile(fidl::Reporter* reporter, std::string library_name, std::string dep_file_path,
            const std::vector<std::string>& source_list,
            const std::vector<std::pair<Behavior, std::string>>& outputs,
            const std::vector<fidl::SourceManager>& source_managers,
            fidl::ExperimentalFlags experimental_flags) {
  fidl::flat::Libraries all_libraries(reporter);
  for (const auto& source_manager : source_managers) {
    if (source_manager.sources().empty()) {
      continue;
    }
    fidl::flat::Compiler compiler(&all_libraries, fidl::ordinals::GetGeneratedOrdinal64,
                                  experimental_flags);
    for (const auto& source_file : source_manager.sources()) {
      if (!Parse(*source_file, reporter, &compiler, experimental_flags)) {
        return 1;
      }
    }
    auto library = compiler.Compile();
    if (!library || !all_libraries.Insert(std::move(library))) {
      return 1;
    }
  }
  if (all_libraries.Empty()) {
    Fail("No library was produced.\n");
  }
  const fidl::flat::Library* target_library = all_libraries.target_library();
  auto unused_libraries = all_libraries.Unused();
  // TODO(fxbug.dev/90838): Remove this once all GN rules only include zx
  // sources when the zx library is actually used.
  if (auto zx_library = all_libraries.Lookup({"zx"})) {
    if (auto iter = unused_libraries.find(zx_library); iter != unused_libraries.end()) {
      // Remove from unused_libraries to avoid reporting an error below.
      unused_libraries.erase(iter);
      // Remove from all_libraries to avoid emitting it in coding tables.
      all_libraries.Remove(zx_library);
    }
  }
  if (!unused_libraries.empty()) {
    std::string message = "Unused libraries provided via --files: ";
    bool first = true;
    for (auto library : unused_libraries) {
      if (first) {
        first = false;
      } else {
        message.append(", ");
      }
      message.append(fidl::NameLibrary(library->name));
    }
    message.append("\n");
    Fail(message.data());
  }

  // Verify that the produced library's name matches the expected name.
  std::string produced_name = fidl::NameLibrary(target_library->name);
  if (!library_name.empty() && produced_name != library_name) {
    Fail("Generated library '%s' did not match --name argument: %s\n", produced_name.data(),
         library_name.data());
  }

  // Write depfile, with format:
  // output1 : inputA inputB inputC
  // output2 : inputA inputB inputC
  // ...
  if (dep_file_path.size() != 0) {
    std::ostringstream dep_file_contents;
    for (auto& output : outputs) {
      auto& file_path = output.second;
      dep_file_contents << file_path << " ";
      dep_file_contents << ": ";
      for (auto& input_path : source_list) {
        dep_file_contents << input_path << " ";
      }
      dep_file_contents << "\n";
    }

    Write(dep_file_contents, dep_file_path);
  }

  // We recompile dependencies, and only emit output for the target library.
  for (auto& output : outputs) {
    auto& behavior = output.first;
    auto& file_path = output.second;

    switch (behavior) {
      case Behavior::kCHeader: {
        fidl::CGenerator generator(target_library);
        Write(generator.ProduceHeader(), file_path);
        break;
      }
      case Behavior::kCClient: {
        fidl::CGenerator generator(target_library);
        Write(generator.ProduceClient(), file_path);
        break;
      }
      case Behavior::kCServer: {
        fidl::CGenerator generator(target_library);
        Write(generator.ProduceServer(), file_path);
        break;
      }
      case Behavior::kTables: {
        fidl::TablesGenerator generator(&all_libraries);
        Write(generator.Produce(), file_path);
        break;
      }
      case Behavior::kJSON: {
        fidl::JSONGenerator generator(&all_libraries, experimental_flags);
        Write(generator.Produce(), file_path);
        break;
      }
    }
  }
  return 0;
}
