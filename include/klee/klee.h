/*===-- klee.h --------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===*/

#ifndef KLEE_H
#define KLEE_H

#include "inttypes.h"
#include "stddef.h"
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif
  
  /* Add an accesible memory object at a user specified location. It
   * is the users responsibility to make sure that these memory
   * objects do not overlap. These memory objects will also
   * (obviously) not correctly interact with external function
   * calls.
   */
  void klee_define_fixed_object(void *addr, size_t nbytes);

  /* klee_make_symbolic - Make the contents of the object pointer to by \arg
   * addr symbolic.
   *
   * \arg addr - The start of the object.
   * \arg nbytes - The number of bytes to make symbolic; currently this *must*
   * be the entire contents of the object.
   * \arg name - A name used for identifying the object in messages, output
   * files, etc. If NULL, object is called "unnamed".
   */
  void klee_make_symbolic(void *addr, size_t nbytes, const char *name);

  /* klee_range - Construct a symbolic value in the signed interval
   * [begin,end).
   *
   * \arg name - A name used for identifying the object in messages, output
   * files, etc. If NULL, object is called "unnamed".
   */
  int klee_range(int begin, int end, const char *name);

  /*  klee_int - Construct an unconstrained symbolic integer.
   *
   * \arg name - An optional name, used for identifying the object in messages,
   * output files, etc.
   */
  int klee_int(const char *name);

  /* klee_silent_exit - Terminate the current KLEE process without generating a
   * test file.
   */
  __attribute__((noreturn))
  void klee_silent_exit(int status);

  /* klee_abort - Abort the current KLEE process. */
  __attribute__((noreturn))
  void klee_abort(void);  

  /* klee_report_error - Report a user defined error and terminate the current
   * KLEE process.
   *
   * \arg file - The filename to report in the error message.
   * \arg line - The line number to report in the error message.
   * \arg message - A string to include in the error message.
   * \arg suffix - The suffix to use for error files.
   */
  __attribute__((noreturn))
  void klee_report_error(const char *file, 
			 int line, 
			 const char *message, 
			 const char *suffix);
  
  /* called by checking code to get size of memory. */
  size_t klee_get_obj_size(void *ptr);
  
  /* print the tree associated w/ a given expression. */
  void klee_print_expr(const char *msg, ...);
  
  /* NB: this *does not* fork n times and return [0,n) in children.
   * It makes n be symbolic and returns: caller must compare N times.
   */
  uintptr_t klee_choose(uintptr_t n);
  
  /* special klee assert macro. this assert should be used when path consistency
   * across platforms is desired (e.g., in tests).
   * NB: __assert_fail is a klee "special" function
   */
# define klee_assert(expr)                                              \
  ((expr)                                                               \
   ? (void) (0)                                                         \
   : __assert_fail (#expr, __FILE__, __LINE__, __PRETTY_FUNCTION__))    \

  /* Return true if the given value is symbolic (represented by an
   * expression) in the current state. This is primarily for debugging
   * and writing tests but can also be used to enable prints in replay
   * mode.
   */
  unsigned klee_is_symbolic(uintptr_t n);


  /* Return true if replaying a concrete test case using the libkleeRuntime library
   * Return false if executing symbolically in KLEE.
   */
  unsigned klee_is_replay();


  /* The following intrinsics are primarily intended for internal use
     and may have peculiar semantics. */

  void klee_assume(uintptr_t condition);
  #define klee_note(condition) (klee_assert(condition), klee_assume(condition))
  void klee_warning(const char *message);
  void klee_warning_once(const char *message);
  void klee_prefer_cex(void *object, uintptr_t condition);
  void klee_posix_prefer_cex(void *object, uintptr_t condition);
  void klee_mark_global(void *object);

  /* Return a possible constant value for the input expression. This
     allows programs to forcibly concretize values on their own. */
#define KLEE_GET_VALUE_PROTO(suffix, type)	type klee_get_value##suffix(type expr)

  KLEE_GET_VALUE_PROTO(f, float);
  KLEE_GET_VALUE_PROTO(d, double);
  KLEE_GET_VALUE_PROTO(l, long);
  KLEE_GET_VALUE_PROTO(ll, long long);
  KLEE_GET_VALUE_PROTO(_i32, int32_t);
  KLEE_GET_VALUE_PROTO(_i64, int64_t);
  KLEE_GET_VALUE_PROTO(_u64, uint64_t);

#undef KLEE_GET_VALUE_PROTO

  /* Ensure that memory in the range [address, address+size) is
     accessible to the program. If some byte in the range is not
     accessible an error will be generated and the state
     terminated. 
  
     The current implementation requires both address and size to be
     constants and that the range lie within a single object. */
  void klee_check_memory_access(const void *address, size_t size);

  /* Enable/disable forking. */
  void klee_set_forking(unsigned enable);

  /* klee_alias_function("foo", "bar") will replace, at runtime (on
     the current path and all paths spawned on the current path), all
     calls to foo() by calls to bar().  foo() and bar() have to exist
     and have identical types.  Use klee_alias_function("foo", "foo")
     to undo.  Be aware that some special functions, such as exit(),
     may not always work. */
  void klee_alias_function(const char* fn_name, const char* new_fn_name);

  /* Similar to klee_alias_function, but uses a regex to match
   the function's name; e.g. klee_alias_function_regex("foo.*", "bar")
   will replace "foo", "foox", "foo123" with "bar".
   Particularly useful to replace a static function, which
   may be replicated many times with a suffixed name. */
  void klee_alias_function_regex(const char *fn_regex, const char *new_fn_name);

  /* Undoes an alias (either a name or a regex). */
  void klee_alias_undo(const char *alias);

  /* Print stack trace. */
  void klee_stack_trace(void);

  /* Print range for given argument and tagged with name */
  void klee_print_range(const char * name, int arg );

  /* Open a merge */
  void klee_open_merge();

  /* Merge all paths of the state that went through klee_open_merge */
  void klee_close_merge();

  /* Get errno value of the current state */
  int klee_get_errno(void);

// Types of the reader/writer for intercepts
/* size is in bytes; value must be zero-extended if size < 8 */
typedef uint64_t (*klee_reader)(uint64_t addr, unsigned offset, unsigned size);
/* size is in bytes; value may be zero-padded if size < 8 */
typedef void (*klee_writer)(uint64_t addr, unsigned offset, unsigned size,
                            uint64_t value);

/*
 * Intercepts reads to the specified block of memory,
 * redirecting them to the specified reader.
 * This can be used for fine-grained access control, or to execute code
 * on each read (such as when modeling hardware).
 */
void klee_intercept_reads(void *addr, const char *reader_name);

/*
 * Intercepts writes to the specified block of memory,
 * redirecting them to the specified writer.
 * This can be used for fine-grained access control, or to execute code
 * on each write (such as when modeling hardware).
 */
void klee_intercept_writes(void *addr, const char *writer_name);

#define KLEE_TRACE_PARAM_PROTO(suffix, type)                                   \
  void klee_trace_param##suffix(type param, const char *name)
KLEE_TRACE_PARAM_PROTO(f, float);
KLEE_TRACE_PARAM_PROTO(d, double);
KLEE_TRACE_PARAM_PROTO(l, long);
KLEE_TRACE_PARAM_PROTO(ll, long long);
KLEE_TRACE_PARAM_PROTO(_u16, uint16_t);
KLEE_TRACE_PARAM_PROTO(_i32, int32_t);
KLEE_TRACE_PARAM_PROTO(_u32, uint32_t);
KLEE_TRACE_PARAM_PROTO(_i64, int64_t);
KLEE_TRACE_PARAM_PROTO(_u64, int64_t);
#undef KLEE_TRACE_PARAM_PROTO

#define KLEE_TRACE_VAL_PROTO(suffix, type)                                     \
  void klee_trace_extra_val##suffix(type param, const char *name,              \
                                    const char *prefix)
KLEE_TRACE_VAL_PROTO(f, float);
KLEE_TRACE_VAL_PROTO(d, double);
KLEE_TRACE_VAL_PROTO(l, long);
KLEE_TRACE_VAL_PROTO(ll, long long);
KLEE_TRACE_VAL_PROTO(_u16, uint16_t);
KLEE_TRACE_VAL_PROTO(_i32, int32_t);
KLEE_TRACE_VAL_PROTO(_u32, uint32_t);
KLEE_TRACE_VAL_PROTO(_i64, int64_t);
KLEE_TRACE_VAL_PROTO(_u64, int64_t);
#undef KLEE_TRACE_VAL_PROTO

void klee_trace_param_ptr(void *ptr, int width, const char *name);
typedef enum TracingDirection {
  TD_NONE = 0,
  TD_IN = 1,
  TD_OUT = 2,
  TD_BOTH = 3
} TracingDirection;
void klee_trace_param_ptr_directed(void *ptr, int width, const char *name,
                                   TracingDirection td);
void klee_trace_param_tagged_ptr(void *ptr, int width, const char *name,
                                 const char *type, TracingDirection td);
void klee_trace_param_just_ptr(void *ptr, int width, const char *name);
void klee_trace_param_fptr(void *ptr, const char *name);
void klee_trace_ret();
void klee_trace_ret_ptr(int width);
void klee_trace_ret_just_ptr(int width);

void klee_trace_param_ptr_field(void *ptr, int offset, int width, char *name);
void klee_trace_param_ptr_field_directed(void *ptr, int offset, int width,
                                         char *name, TracingDirection td);
void klee_trace_param_ptr_field_just_ptr(void *ptr, int offset, int width,
                                         char *name);
void klee_trace_ret_ptr_field(int offset, int width, char *name);
void klee_trace_ret_ptr_field_just_ptr(int offset, int width, char *name);
void klee_trace_param_ptr_nested_field(void *ptr, int base_offset, int offset,
                                       int width, char *name);
void klee_trace_param_ptr_nested_field_directed(void *ptr, int base_offset,
                                                int offset, int width,
                                                char *name,
                                                TracingDirection td);
void klee_trace_ret_ptr_nested_field(int base_offset, int offset, int width,
                                     char *name);
void klee_trace_extra_ptr(void *ptr, int width, char *name, char *type,
                          char *prefix, TracingDirection td);
void klee_trace_extra_fptr(void *ptr, int width, char *name, char *type,
                           char *prefix, TracingDirection td);
void klee_trace_extra_ptr_field(void *ptr, int offset, int width, char *name,
                                TracingDirection td);
void klee_trace_extra_ptr_field_just_ptr(void *ptr, int offset, int width,
                                         char *name);
void klee_trace_extra_ptr_nested_field(void *ptr, int base_offset, int offset,
                                       int width, char *name,
                                       TracingDirection td);
void klee_trace_extra_ptr_nested_nested_field(void *ptr, int base_base_offset,
                                              int base_offset, int offset,
                                              int width, char *name,
                                              TracingDirection td);

int klee_induce_invariants();

void klee_forbid_access(void *ptr, int width, char *message);
void klee_allow_access(void *ptr, int width);

void klee_dump_constraints();

void klee_possibly_havoc(void *ptr, int width, char *name);

int traced_variable_type(char *variable, char **type);

void klee_map_symbol_names(char* symbol_name, int occurence, void*key, int width);

void klee_add_bpf_call();

#define PERF_MODEL_BRANCH(param, val1, val2)                                   \
  if (param) {                                                                 \
    param = val1;                                                              \
  } else {                                                                     \
    param = val2;                                                              \
    /* Setting it to a different value so compiler doesn't eliminate branch */ \
  }

#define TRACE_VAR(var, name)                                                   \
  klee_assert(traced_variable_type(name, &prefix) &&                           \
              "Prefix for Variable not found");                                \
  klee_trace_extra_ptr(&var, sizeof(var), name, "type", prefix, TD_BOTH);

#define TRACE_FPTR(fptr, name)                                                 \
  klee_assert(traced_variable_type(name, &prefix) &&                           \
              "Prefix for Variable not found");                                \
  klee_trace_extra_fptr(fptr, sizeof(fptr), name, "type", prefix, TD_BOTH);

#define TRACE_VAL(var, name, type)                                             \
  klee_assert(traced_variable_type(name, &prefix) &&                           \
              "Prefix for Variable not found");                                \
  klee_trace_extra_val##type(var, name, prefix);

#define DS_PATH(num) __attribute__((noinline)) void ds_path_##num()
DS_PATH(1);
DS_PATH(2);
DS_PATH(3);
#undef DS_PATH

#ifdef __cplusplus
}
#endif

#endif /* KLEE_H */
