// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/ref_counted.h"

namespace {

class Parent {
};

class Child : public Parent {
};

class RefCountedClass : public base::RefCountedThreadSafe<RefCountedClass> {
};

}  // namespace

#if defined(NCTEST_NO_PASS_DOWNCAST)  // [r"fatal error: no matching constructor for initialization of 'base::internal::scoped_ptr_impl<\(anonymous namespace\)::Child, base::DefaultDeleter<\(anonymous namespace\)::Child> >::Data'"]

std::unique_ptr<Child> DowncastUsingPassAs(std::unique_ptr<Parent> object) {
  return object.Pass();
}

#elif defined(NCTEST_NO_REF_COUNTED_SCOPED_PTR)  // [r"fatal error: static_assert failed \"T_is_refcounted_type_and_needs_scoped_refptr\""]

// std::unique_ptr<> should not work for ref-counted objects.
void WontCompile() {
  std::unique_ptr<RefCountedClass> x;
}

#elif defined(NCTEST_NO_ARRAY_WITH_SIZE)  // [r"fatal error: static_assert failed \"do_not_use_array_with_size_as_type\""]

void WontCompile() {
  std::unique_ptr<int[10]> x;
}

#elif defined(NCTEST_NO_PASS_FROM_ARRAY)  // [r"fatal error: static_assert failed \"U_cannot_be_an_array\""]

void WontCompile() {
  std::unique_ptr<int[]> a;
  std::unique_ptr<int*> b;
  b = a.Pass();
}

#elif defined(NCTEST_NO_PASS_TO_ARRAY)  // [r"fatal error: no viable overloaded '='"]

void WontCompile() {
  std::unique_ptr<int*> a;
  std::unique_ptr<int[]> b;
  b = a.Pass();
}

#elif defined(NCTEST_NO_CONSTRUCT_FROM_ARRAY)  // [r"fatal error: 'impl_' is a private member of 'std::unique_ptr<int \[\], base::DefaultDeleter<int \[\]> >'"]

void WontCompile() {
  std::unique_ptr<int[]> a;
  std::unique_ptr<int*> b(a.Pass());
}

#elif defined(NCTEST_NO_CONSTRUCT_TO_ARRAY)  // [r"fatal error: no matching constructor for initialization of 'std::unique_ptr<int \[\]>'"]

void WontCompile() {
  std::unique_ptr<int*> a;
  std::unique_ptr<int[]> b(a.Pass());
}

#elif defined(NCTEST_NO_CONSTRUCT_SCOPED_PTR_ARRAY_FROM_NULL)  // [r"is ambiguous"]

void WontCompile() {
  std::unique_ptr<int[]> x(NULL);
}

#elif defined(NCTEST_NO_CONSTRUCT_SCOPED_PTR_ARRAY_FROM_DERIVED)  // [r"fatal error: calling a private constructor of class 'std::unique_ptr<\(anonymous namespace\)::Parent \[\], base::DefaultDeleter<\(anonymous namespace\)::Parent \[\]> >'"]

void WontCompile() {
  std::unique_ptr<Parent[]> x(new Child[1]);
}

#elif defined(NCTEST_NO_RESET_SCOPED_PTR_ARRAY_FROM_NULL)  // [r"is ambiguous"]

void WontCompile() {
  std::unique_ptr<int[]> x;
  x.reset(NULL);
}

#elif defined(NCTEST_NO_RESET_SCOPED_PTR_ARRAY_FROM_DERIVED)  // [r"fatal error: 'reset' is a private member of 'std::unique_ptr<\(anonymous namespace\)::Parent \[\], base::DefaultDeleter<\(anonymous namespace\)::Parent \[\]> >'"]

void WontCompile() {
  std::unique_ptr<Parent[]> x;
  x.reset(new Child[1]);
}

#elif defined(NCTEST_NO_DELETER_REFERENCE)  // [r"fatal error: base specifier must name a class"]

struct Deleter {
  void operator()(int*) {}
};

// Current implementation doesn't support Deleter Reference types. Enabling
// support would require changes to the behavior of the constructors to match
// including the use of SFINAE to discard the type-converting constructor
// as per C++11 20.7.1.2.1.19.
void WontCompile() {
  Deleter d;
  int n;
  std::unique_ptr<int*, Deleter&> a(&n, d);
}

#endif
