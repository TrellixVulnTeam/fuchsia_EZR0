// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(GeneratedNameTests, GoodInsideStruct) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") struct {};
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodInsideTable) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = table {
  1: bar @generated_name("Good") struct {};
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupTable("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].maybe_used->type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodInsideUnion) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = union {
  1: bar @generated_name("Good") struct {};
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupUnion("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].maybe_used->type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodInsideRequest) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {
  Bar(@generated_name("Good") struct { x uint32; });
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupProtocol("Foo");

  // TODO(fxbug.dev/87028): Assert that Foo exists, and that the anonymous
  // struct gets named "Good".
  ASSERT_NULL(foo);
}

TEST(GeneratedNameTests, GoodInsideResponse) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {
  Bar() -> (@generated_name("Good") struct { x uint32; });
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupProtocol("Foo");

  // TODO(fxbug.dev/87028): Assert that Foo exists, and that the anonymous
  // struct gets named "Good".
  ASSERT_NULL(foo);
}

TEST(GeneratedNameTests, GoodInsideResultSuccess) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {
  Bar() -> (@generated_name("Good") struct { x uint32; }) error uint32;
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupProtocol("Foo");

  // TODO(fxbug.dev/87028): Assert that Foo exists, and that the anonymous
  // struct gets named "Good".
  ASSERT_NULL(foo);
}

TEST(GeneratedNameTests, GoodInsideResultError) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {
  Bar() -> (struct {}) error @generated_name("Good") enum { A = 1; };
};

)FIDL");
  ASSERT_COMPILED(library)
  auto foo = library.LookupProtocol("Foo");
  ASSERT_NOT_NULL(foo);
  auto* response_type = foo->methods[0].maybe_response_payload;
  auto result_type = response_type->members[0].type_ctor->type;
  auto* result_union = library.LookupUnion(std::string(result_type->name.decl_name()));
  auto* error_type = result_union->members[1].maybe_used->type_ctor->type;

  // TODO(fxbug.dev/85453): Should be named "Good".
  EXPECT_EQ(error_type->name.decl_name(), "Foo_Bar_Error");
}

TEST(GeneratedNameTests, GoodOnBits) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") bits {
    A = 1;
  };
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;

  // TODO(fxbug.dev/84104): Should be named "Good".
  EXPECT_EQ(bar_type->name.decl_name(), "Bar");
}

TEST(GeneratedNameTests, GoodOnEnum) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") enum {
    A = 1;
  };
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;

  // TODO(fxbug.dev/84104): Should be named "Good".
  EXPECT_EQ(bar_type->name.decl_name(), "Bar");
}

TEST(GeneratedNameTests, GoodOnStruct) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") struct {
    x uint32;
  };
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodOnTable) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") table {
    1: x uint32;
  };
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodOnUnion) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Good") union {
    1: x uint32;
  };
};

)FIDL");
  ASSERT_COMPILED(library);
  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  auto bar_type = foo->members[0].type_ctor->type;
  EXPECT_EQ(bar_type->name.decl_name(), "Good");
}

TEST(GeneratedNameTests, GoodPreventsCollision) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  foo @generated_name("Bar") struct {};
};

)FIDL");
  ASSERT_COMPILED(library);
}

TEST(GeneratedNameTests, BadOnTypeDeclaration) {
  TestLibrary library(R"FIDL(
library fidl.test;

@generated_name("Good")
type Bad = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(GeneratedNameTests, BadOnTopLevelStruct) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = @generated_name("Bad") struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(GeneratedNameTests, BadOnIdentifierType) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bar @generated_name("Bad") Bar;
};

type Bar = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotAttachAttributeToIdentifier);
}

TEST(GeneratedNameTests, BadOnEnumMember) {
  TestLibrary library(R"FIDL(
library fidl.test;

type MetaVars = enum {
  FOO = 1;
  @generated_name("BAD")
  BAR = 2;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(GeneratedNameTests, BadOnServiceMember) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {};

service Bar {
  @generated_name("One")
  bar_one client_end:Foo;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(GeneratedNameTests, BadMissingArgument) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bad @generated_name struct {};
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingRequiredAnonymousAttributeArg);
}

TEST(GeneratedNameTests, BadInvalidIdentifier) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bad @generated_name("ez$") struct {};
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidGeneratedName);
}

TEST(GeneratedNameTests, BadNameCollision) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  foo @generated_name("Baz") struct {};
};

type Baz = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(GeneratedNameTests, BadNonLiteralArgument) {
  TestLibrary library(R"FIDL(
library fidl.test;

const NAME string = "baz";

type Foo = struct {
  bar @generated_name(NAME) struct {};
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgDisallowsConstants);
}

}  // namespace
