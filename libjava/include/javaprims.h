// javaprims.h - Main external header file for libgcj.  -*- c++ -*-

/* Copyright (C) 1998, 1999  Cygnus Solutions

   This file is part of libgcj.

This software is copyrighted work licensed under the terms of the
Libgcj License.  Please consult the file "LIBGCJ_LICENSE" for
details.  */

#ifndef __JAVAPRIMS_H__
#define __JAVAPRIMS_H__

// To force selection of correct types that will mangle consistently
// across platforms.
extern "Java"
{
  typedef __java_byte jbyte;
  typedef __java_short jshort;
  typedef __java_int jint;
  typedef __java_long jlong;
  typedef __java_float jfloat;
  typedef __java_double jdouble;
  typedef __java_char jchar;
  typedef __java_boolean jboolean;
  typedef jint jsize;

  // The following class declarations are automatically generated by
  // the `classes.pl' script.
  namespace java
  {
    namespace io
    {
      class BufferedInputStream;
      class BufferedOutputStream;
      class BufferedReader;
      class BufferedWriter;
      class ByteArrayInputStream;
      class ByteArrayOutputStream;
      class CharArrayReader;
      class CharArrayWriter;
      class CharConversionException;
      class DataInput;
      class DataInputStream;
      class DataOutput;
      class DataOutputStream;
      class EOFException;
      class File;
      class FileDescriptor;
      class FileInputStream;
      class FileNotFoundException;
      class FileOutputStream;
      class FileReader;
      class FileWriter;
      class FilenameFilter;
      class FilterInputStream;
      class FilterOutputStream;
      class FilterReader;
      class FilterWriter;
      class IOException;
      class InputStream;
      class InputStreamReader;
      class InterruptedIOException;
      class LineNumberInputStream;
      class LineNumberReader;
      class OutputStream;
      class OutputStreamWriter;
      class PipedInputStream;
      class PipedOutputStream;
      class PipedReader;
      class PipedWriter;
      class PrintStream;
      class PrintWriter;
      class PushbackInputStream;
      class PushbackReader;
      class RandomAccessFile;
      class Reader;
      class SequenceInputStream;
      class Serializable;
      class StreamTokenizer;
      class StringBufferInputStream;
      class StringReader;
      class StringWriter;
      class SyncFailedException;
      class UTFDataFormatException;
      class UnsupportedEncodingException;
      class Writer;
    };

    namespace lang
    {
      class AbstractMethodError;
      class ArithmeticException;
      class ArrayIndexOutOfBoundsException;
      class ArrayStoreException;
      class Boolean;
      class Byte;
      class Character;
      class Class;
      class ClassCastException;
      class ClassCircularityError;
      class ClassFormatError;
      class ClassLoader;
      class ClassNotFoundException;
      class CloneNotSupportedException;
      class Cloneable;
      class Comparable;
      class Compiler;
      class ConcreteProcess;
      class Double;
      class Error;
      class Exception;
      class ExceptionInInitializerError;
      class FirstThread;
      class Float;
      class IllegalAccessError;
      class IllegalAccessException;
      class IllegalArgumentException;
      class IllegalMonitorStateException;
      class IllegalStateException;
      class IllegalThreadStateException;
      class IncompatibleClassChangeError;
      class IndexOutOfBoundsException;
      class InstantiationError;
      class InstantiationException;
      class Integer;
      class InternalError;
      class InterruptedException;
      class LinkageError;
      class Long;
      class Math;
      class NegativeArraySizeException;
      class NoClassDefFoundError;
      class NoSuchFieldError;
      class NoSuchFieldException;
      class NoSuchMethodError;
      class NoSuchMethodException;
      class NullPointerException;
      class Number;
      class NumberFormatException;
      class Object;
      class OutOfMemoryError;
      class Process;
      class Runnable;
      class Runtime;
      class RuntimeException;
      class SecurityException;
      class SecurityManager;
      class Short;
      class StackOverflowError;
      class String;
      class StringBuffer;
      class StringIndexOutOfBoundsException;
      class System;
      class Thread;
      class ThreadDeath;
      class ThreadGroup;
      class Throwable;
      class UnknownError;
      class UnsatisfiedLinkError;
      class UnsupportedOperationException;
      class VerifyError;
      class VirtualMachineError;
      class Void;
      class VMClassLoader;
      namespace reflect
      {
        class AccessibleObject;
        class Array;
        class Constructor;
        class Field;
        class InvocationTargetException;
        class Member;
        class Method;
        class Modifier;
      };
    };

    namespace util
    {
      class BitSet;
      class Calendar;
      class ConcurrentModificationException;
      class Date;
      class Dictionary;
      class EmptyStackException;
      class Enumeration;
      class EventListener;
      class EventObject;
      class GregorianCalendar;
      class Hashtable;
      class HashtableEntry;
      class HashtableEnumeration;
      class ListResourceBundle;
      class Locale;
      class MissingResourceException;
      class NoSuchElementException;
      class Observable;
      class Observer;
      class Properties;
      class PropertyResourceBundle;
      class Random;
      class ResourceBundle;
      class SimpleTimeZone;
      class Stack;
      class StringTokenizer;
      class TimeZone;
      class TooManyListenersException;
      class Vector;
      class VectorEnumeration;
      namespace zip
      {
        class Adler32;
        class CRC32;
        class CheckedInputStream;
        class CheckedOutputStream;
        class Checksum;
        class DataFormatException;
        class Deflater;
        class DeflaterOutputStream;
        class GZIPInputStream;
        class GZIPOutputStream;
        class Inflater;
        class InflaterInputStream;
        class ZipConstants;
        class ZipEntry;
        class ZipEnumeration;
        class ZipException;
        class ZipFile;
        class ZipInputStream;
        class ZipOutputStream;
      };
    };
  };
};

typedef struct java::lang::Object* jobject;
typedef class java::lang::Class* jclass;
typedef class java::lang::Throwable* jthrowable;
typedef class java::lang::String* jstring;
struct _Jv_JNIEnv;

typedef struct _Jv_Field *jfieldID;
typedef struct _Jv_Method *jmethodID;

extern "C" jobject _Jv_AllocObject (jclass, jint);
extern "C" jboolean _Jv_IsInstanceOf(jobject, jclass);
extern "C" jstring _Jv_AllocString(jsize);
extern "C" jstring _Jv_NewString (const jchar*, jsize);
extern "C" jchar* _Jv_GetStringChars (jstring str);
extern "C" jint _Jv_MonitorEnter (jobject);
extern "C" jint _Jv_MonitorExit (jobject);
extern "C" jstring _Jv_NewStringLatin1(const char*, jsize);
extern "C" jsize _Jv_GetStringUTFLength (jstring);
extern "C" jsize _Jv_GetStringUTFRegion (jstring, jsize, jsize, char *);

extern "C" void _Jv_Throw (void *) __attribute__ ((__noreturn__));
extern "C" void* _Jv_Malloc (jsize);
extern "C" void _Jv_Free (void*);

typedef unsigned short _Jv_ushort __attribute__((__mode__(__HI__)));
typedef unsigned int _Jv_uint __attribute__((__mode__(__SI__)));

struct _Jv_Utf8Const
{
  _Jv_ushort hash;
  _Jv_ushort length;	/* In bytes, of data portion, without final '\0'. */
  char data[1];		/* In Utf8 format, with final '\0'. */
};

#endif /* __JAVAPRIMS_H__ */
