//===-- KTest.h --------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_KTEST_H
#define KLEE_KTEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

  typedef struct KTestObject KTestObject;
  struct KTestObject {
    char *name;
    unsigned numBytes;
    unsigned char *bytes;
  };

  typedef struct KTestHavocedLocation KTestHavocedLocation;
  struct KTestHavocedLocation {
    char *name;
    unsigned numBytes;
    unsigned char *bytes;
    uint32_t *mask;
  };
  
  typedef struct KTest KTest;
  struct KTest {
    /* file format version */
    unsigned version; 
    
    unsigned numArgs;
    char **args;

    unsigned symArgvs;
    unsigned symArgvLen;

    unsigned numObjects;
    KTestObject *objects;
    unsigned numHavocs;
    KTestHavocedLocation *havocs;
  };

  
  /* returns the current .ktest file format version */
  unsigned kTest_getCurrentVersion();
  
  /* return true iff file at path matches KTest header */
  int   kTest_isKTestFile(const char *path);

  /* returns NULL on (unspecified) error */
  KTest* kTest_fromFile(const char *path);

  /* returns 1 on success, 0 on (unspecified) error */
  int   kTest_toFile(KTest *, const char *path);
  
  /* returns total number of object bytes */
  unsigned kTest_numBytes(KTest *);

  void  kTest_free(KTest *);

#ifdef __cplusplus
}
#endif

#endif /* KLEE_KTEST_H */
