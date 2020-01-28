// Copyright 2020 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <Python.h>
#include <stddef.h>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rcutils/types/rcutils_ret.h"

#include "rclpy_common/handle.h"

struct rclpy_handle_t
{
  void * ptr;  // opaque pointer to the wrapped object.
  size_t ref_count;  // Reference count.
  struct rclpy_handle_t ** dependencies;  // array of pointers to dependencies.
  size_t num_of_dependencies;  // size of the array.
  rclpy_handle_destructor_t destructor;  // destructor
};

/// Creates a Handle object.
rclpy_handle_t *
_rclpy_create_handle(void * ptr, rclpy_handle_destructor_t destructor)
{
  assert(ptr);
  assert(destructor);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rclpy_handle_t * handle = allocator.zero_allocate(1, sizeof(rclpy_handle_t), allocator.state);
  if (!handle) {
    return NULL;
  }

  handle->ptr = ptr;
  handle->ref_count++;
  handle->destructor = destructor;

  return handle;
}

/// Adds a dependency to a handle.
/**
 * The `dependency` handle reference count is incresead.
 * The `dependent` handle stores `dependency` into its dependencies list.
 */
rcutils_ret_t
_rclpy_handle_add_dependency(rclpy_handle_t * dependent, rclpy_handle_t * dependency)
{
  assert(dependent);
  assert(dependency);

  rcutils_allocator_t allocator = rcutils_get_default_allocator();

  rclpy_handle_t ** new_dependencies = allocator.reallocate(
    dependent->dependencies,
    (dependent->num_of_dependencies + 1u) * sizeof(rclpy_handle_t *),
    allocator.state);
  if (!new_dependencies) {
    return RCUTILS_RET_ERROR;
  }
  new_dependencies[dependent->num_of_dependencies] = dependency;
  dependent->num_of_dependencies++;
  dependent->dependencies = new_dependencies;
  dependency->ref_count++;
  return RCUTILS_RET_OK;
}

/// Decrements the reference count of a handle.
/**
 * The reference count of `handle` is decremented.
 * If it reaches zero:
 * - `rclpy_handle_dec_ref` is called on `handle` dependencies.
 * - `handle` is deallocated.
 */
void
_rclpy_handle_dec_ref(rclpy_handle_t * handle)
{
  if (!handle) {
    return;
  }
  assert(
    (0u != handle->num_of_dependencies && NULL != handle->dependencies) ||
    (0u == handle->num_of_dependencies && NULL == handle->dependencies));

  if (!--handle->ref_count) {
    if (handle->destructor) {
      handle->destructor(handle->ptr);
    }
    for (size_t i = 0; i < handle->num_of_dependencies; i++) {
      _rclpy_handle_dec_ref(handle->dependencies[i]);
    }
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    allocator.deallocate(handle->dependencies, allocator.state);
    allocator.deallocate(handle, allocator.state);
  }
}

void
_rclpy_handle_capsule_destructor(PyObject * capsule)
{
  rclpy_handle_t * handle = PyCapsule_GetPointer(capsule, PyCapsule_GetName(capsule));
  if (!handle) {
    return;
  }
  _rclpy_handle_dec_ref(handle);
}

/// Creates a PyCapsule wrapping a handle object.
PyObject *
rclpy_create_handle_capsule(void * ptr, const char * name, rclpy_handle_destructor_t destructor)
{
  rclpy_handle_t * handle = _rclpy_create_handle(ptr, destructor);
  if (!handle) {
    return NULL;
  }
  return PyCapsule_New(handle, name, _rclpy_handle_capsule_destructor);
}

void *
_rclpy_handle_get_pointer(rclpy_handle_t * handle)
{
  if (!handle) {
    return NULL;
  }
  return handle->ptr;
}

void *
rclpy_handle_get_pointer_from_capsule(PyObject * capsule, const char * name)
{
  rclpy_handle_t * handle = PyCapsule_GetPointer(capsule, name);
  return _rclpy_handle_get_pointer(handle);
}
