// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Scopers help you manage ownership of a pointer, helping you easily manage a
// pointer within a scope, and automatically destroying the pointer at the end
// of a scope.  There are two main classes you will use, which correspond to the
// operators new/delete and new[]/delete[].
//
// Example usage (std::unique_ptr<T>):
//   {
//     std::unique_ptr<Foo> foo(new Foo("wee"));
//   }  // foo goes out of scope, releasing the pointer with it.
//
//   {
//     std::unique_ptr<Foo> foo;          // No pointer managed.
//     foo.reset(new Foo("wee"));    // Now a pointer is managed.
//     foo.reset(new Foo("wee2"));   // Foo("wee") was destroyed.
//     foo.reset(new Foo("wee3"));   // Foo("wee2") was destroyed.
//     foo->Method();                // Foo::Method() called.
//     foo.get()->Method();          // Foo::Method() called.
//     SomeFunc(foo.release());      // SomeFunc takes ownership, foo no longer
//                                   // manages a pointer.
//     foo.reset(new Foo("wee4"));   // foo manages a pointer again.
//     foo.reset();                  // Foo("wee4") destroyed, foo no longer
//                                   // manages a pointer.
//   }  // foo wasn't managing a pointer, so nothing was destroyed.
//
// Example usage (std::unique_ptr<T[]>):
//   {
//     std::unique_ptr<Foo[]> foo(new Foo[100]);
//     foo.get()->Method();  // Foo::Method on the 0th element.
//     foo[10].Method();     // Foo::Method on the 10th element.
//   }
//
// These scopers also implement part of the functionality of C++11 unique_ptr
// in that they are "movable but not copyable."  You can use the scopers in
// the parameter and return types of functions to signify ownership transfer
// in to and out of a function.  When calling a function that has a scoper
// as the argument type, it must be called with the result of an analogous
// scoper's Pass() function or another function that generates a temporary;
// passing by copy will NOT work.  Here is an example using scoped_ptr:
//
//   void TakesOwnership(std::unique_ptr<Foo> arg) {
//     // Do something with arg
//   }
//   std::unique_ptr<Foo> CreateFoo() {
//     // No need for calling Pass() because we are constructing a temporary
//     // for the return value.
//     return std::unique_ptr<Foo>(new Foo("new"));
//   }
//   std::unique_ptr<Foo> PassThru(std::unique_ptr<Foo> arg) {
//     return arg.Pass();
//   }
//
//   {
//     std::unique_ptr<Foo> ptr(new Foo("yay"));  // ptr manages Foo("yay").
//     TakesOwnership(ptr.Pass());           // ptr no longer owns Foo("yay").
//     std::unique_ptr<Foo> ptr2 = CreateFoo();   // ptr2 owns the return Foo.
//     std::unique_ptr<Foo> ptr3 =                // ptr3 now owns what was in ptr2.
//         PassThru(ptr2.Pass());            // ptr2 is correspondingly nullptr.
//   }
//
// Notice that if you do not call Pass() when returning from PassThru(), or
// when invoking TakesOwnership(), the code will not compile because scopers
// are not copyable; they only implement move semantics which require calling
// the Pass() function to signify a destructive transfer of state. CreateFoo()
// is different though because we are constructing a temporary on the return
// line and thus can avoid needing to call Pass().
//
// Pass() properly handles upcast in initialization, i.e. you can use a
// std::unique_ptr<Child> to initialize a std::unique_ptr<Parent>:
//
//   std::unique_ptr<Foo> foo(new Foo());
//   std::unique_ptr<FooParent> parent(foo.Pass());

#ifndef BASE_MEMORY_SCOPED_PTR_H_
#define BASE_MEMORY_SCOPED_PTR_H_

// This is an implementation designed to match the anticipated future TR2
// implementation of the scoped_ptr class.

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include <algorithm>  // For std::swap().

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/move.h"
#include "base/template_util.h"

namespace base {

namespace subtle {
class RefCountedBase;
class RefCountedThreadSafeBase;
}  // namespace subtle

// Function object which deletes its parameter, which must be a pointer.
// If C is an array type, invokes 'delete[]' on the parameter; otherwise,
// invokes 'delete'. The default deleter for std::unique_ptr<T>.
template <class T>
struct DefaultDeleter {
  DefaultDeleter() {}
  template <typename U> DefaultDeleter(const DefaultDeleter<U>& other) {
    // IMPLEMENTATION NOTE: C++11 20.7.1.1.2p2 only provides this constructor
    // if U* is implicitly convertible to T* and U is not an array type.
    //
    // Correct implementation should use SFINAE to disable this
    // constructor. However, since there are no other 1-argument constructors,
    // using a COMPILE_ASSERT() based on is_convertible<> and requiring
    // complete types is simpler and will cause compile failures for equivalent
    // misuses.
    //
    // Note, the is_convertible<U*, T*> check also ensures that U is not an
    // array. T is guaranteed to be a non-array, so any U* where U is an array
    // cannot convert to T*.
    enum { T_must_be_complete = sizeof(T) };
    enum { U_must_be_complete = sizeof(U) };
    COMPILE_ASSERT((base::is_convertible<U*, T*>::value),
                   U_ptr_must_implicitly_convert_to_T_ptr);
  }
  inline void operator()(T* ptr) const {
    enum { type_must_be_complete = sizeof(T) };
    delete ptr;
  }
};

// Specialization of DefaultDeleter for array types.
template <class T>
struct DefaultDeleter<T[]> {
  inline void operator()(T* ptr) const {
    enum { type_must_be_complete = sizeof(T) };
    delete[] ptr;
  }

 private:
  // Disable this operator for any U != T because it is undefined to execute
  // an array delete when the static type of the array mismatches the dynamic
  // type.
  //
  // References:
  //   C++98 [expr.delete]p3
  //   http://cplusplus.github.com/LWG/lwg-defects.html#938
  template <typename U> void operator()(U* array) const;
};

template <class T, int n>
struct DefaultDeleter<T[n]> {
  // Never allow someone to declare something like std::unique_ptr<int[10]>.
  COMPILE_ASSERT(sizeof(T) == -1, do_not_use_array_with_size_as_type);
};

// Function object which invokes 'free' on its parameter, which must be
// a pointer. Can be used to store malloc-allocated pointers in scoped_ptr:
//
// std::unique_ptr<int, base::FreeDeleter> foo_ptr(
//     static_cast<int*>(malloc(sizeof(int))));
struct FreeDeleter {
  inline void operator()(void* ptr) const {
    free(ptr);
  }
};

namespace internal {

template <typename T> struct IsNotRefCounted {
  enum {
    value = !base::is_convertible<T*, base::subtle::RefCountedBase*>::value &&
        !base::is_convertible<T*, base::subtle::RefCountedThreadSafeBase*>::
            value
  };
};

template <typename T>
struct ShouldAbortOnSelfReset {
  template <typename U>
  static NoType Test(const typename U::AllowSelfReset*);

  template <typename U>
  static YesType Test(...);

  static const bool value = sizeof(Test<T>(0)) == sizeof(YesType);
};

}  // namespace internal

}  // namespace base

#endif  // BASE_MEMORY_SCOPED_PTR_H_
