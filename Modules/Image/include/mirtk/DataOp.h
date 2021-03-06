/*
 * Medical Image Registration ToolKit (MIRTK)
 *
 * Copyright 2013-2015 Imperial College London
 * Copyright 2013-2015 Andreas Schuh
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


/// @file  mirtk/DataOp.h
/// @brief Defines base class and I/O functions for arbitrary 1D data sequences
///
/// Functions to manipulate the data are defined in mirtkDataFunctions.h.
/// Statistics of the data sequence such as mean and variance or percentiles
/// can be computed using the operators found in mirtkDataStatistics.h.
/// The data operators are used in particular by the calculate.cc tool for
/// which they were originally developed. They were added to the linear
/// algebra library because they are useful to compute common statistics or
/// perform basic mathematical operations on a data sequence such as an image
/// or attributes of a VTK point set.
///
/// @sa mirtkDataFunctions.h
/// @sa mirtkDataStatistics.h
#ifndef MIRTK_DataOp_H
#define MIRTK_DataOp_H

#include "mirtk/ImageConfig.h"

#include "mirtk/Math.h"
#include "mirtk/Stream.h"
#include "mirtk/Voxel.h"
#include "mirtk/ImageAttributes.h"

#if MIRTK_Image_WITH_VTK
#  include "vtkSmartPointer.h"
#  include "vtkDataSet.h"
#  include "vtkDataArray.h"
#endif


namespace mirtk { namespace data {


// =============================================================================
// Base class of data operations
// =============================================================================

// -----------------------------------------------------------------------------
/// Base class of all data operations
class Op
{
public:

  /// Destructor
  virtual ~Op() {}

  /// Process given data
  virtual void Process(int, double *, bool * = NULL) = 0;

#if MIRTK_Image_WITH_VTK
  /// Process given vtkDataArray
  virtual void Process(vtkDataArray *data, bool *mask = NULL)
  {
    const int n = static_cast<int>(data->GetNumberOfTuples() * data->GetNumberOfComponents());
    if (data->GetDataType() == VTK_DOUBLE) {
      this->Process(n, reinterpret_cast<double *>(data->GetVoidPointer(0)), mask);
    } else {
      double *d = new double[n];
      double *tuple = d;
      for (vtkIdType i = 0; i < data->GetNumberOfTuples(); ++i) {
        data->GetTuple(i, tuple);
        tuple += data->GetNumberOfComponents();
      }
      this->Process(n, d, mask);
      for (vtkIdType i = 0; i < data->GetNumberOfTuples(); ++i) {
        data->SetTuple(i, tuple);
        tuple += data->GetNumberOfComponents();
      }
      delete[] d;
    }
  }
#endif
};

// =============================================================================
// I/O functions
// =============================================================================

// -----------------------------------------------------------------------------
/// Enumeration of supported input data file types
enum DataFileType
{
  UnknownDataFile,
  IMAGE,
  LEGACY_VTK,
  XML_VTK
};

/// Get (or guess) type of input file
DataFileType FileType(const char *name);

// -----------------------------------------------------------------------------
/// Read data sequence from any supported input file type
#if MIRTK_Image_WITH_VTK
int Read(const char *name, double *&data, int *dtype = NULL, ImageAttributes *attr = NULL,
         vtkSmartPointer<vtkDataSet> *dataset= NULL, const char *scalar_name = NULL);
#else
int Read(const char *name, double *&data, int *dtype = NULL, ImageAttributes *attr = NULL);
#endif // MIRTK_Image_WITH_VTK

// -----------------------------------------------------------------------------
/// Write data sequence
class Write : public Op
{
  /// Name of output file
  mirtkPublicAttributeMacro(string, FileName);

#if MIRTK_Image_WITH_VTK

  /// VTK input dataset whose scalar data was modified
  mirtkPublicAttributeMacro(vtkSmartPointer<vtkDataSet>, DataSet);

  /// Name of input/output point data array
  mirtkPublicAttributeMacro(string, ArrayName);

#endif // MIRTK_Image_WITH_VTK

  /// Attributes of input image whose data was modified
  mirtkPublicAttributeMacro(ImageAttributes, Attributes);

  /// Output data type
  mirtkPublicAttributeMacro(int, DataType);

public:

  /// Constructor
#if MIRTK_Image_WITH_VTK

  Write(const char *fname, int dtype = MIRTK_VOXEL_DOUBLE,
        ImageAttributes attr = ImageAttributes(),
        vtkDataSet *dataset      = NULL,
        const char *scalars_name = NULL)
  :
    _FileName(fname),
    _DataSet(dataset),
    _Attributes(attr),
    _DataType(dtype)
  {
    if (scalars_name) _ArrayName = scalars_name;
  }

#else // MIRTK_Image_WITH_VTK

  Write(const char *fname, int dtype = MIRTK_VOXEL_DOUBLE,
        ImageAttributes attr = ImageAttributes())
  :
    _FileName(fname),
    _Attributes(attr),
    _DataType(dtype)
  {}

#endif // MIRTK_Image_WITH_VTK

  /// Process given data
  virtual void Process(int n, double *data, bool * = NULL);

};

// =============================================================================
// Auxiliary macros for subclass implementation
// =============================================================================

// -----------------------------------------------------------------------------
// Add Calculate function that takes a vtkDataArray as argument
// and computes a single return value
//
// Subclass must implement:
//   template <class T> double Calculate(int n, const T *data, const bool *mask)
#if MIRTK_Image_WITH_VTK
  #define mirtkCalculateVtkDataArray1() \
    static double Calculate(vtkDataArray *data, const bool *mask = NULL) \
    { \
      const void *p = data->GetVoidPointer(0); \
      const int   n = static_cast<int>(data->GetNumberOfTuples() * \
                                       data->GetNumberOfComponents()); \
      switch (data->GetDataType()) { \
        case VTK_SHORT:  return Calculate(n, reinterpret_cast<const short  *>(p), mask); \
        case VTK_INT:    return Calculate(n, reinterpret_cast<const int    *>(p), mask); \
        case VTK_FLOAT:  return Calculate(n, reinterpret_cast<const float  *>(p), mask); \
        case VTK_DOUBLE: return Calculate(n, reinterpret_cast<const double *>(p), mask); \
        default: \
          cerr << "Unsupported vtkDataArray type: " << data->GetDataType() << endl; \
          exit(1); \
      } \
      return numeric_limits<double>::quiet_NaN(); \
    }
#else
  #define mirtkCalculateVtkDataArray1()
#endif

// -----------------------------------------------------------------------------
// Add Calculate function that takes a vtkDataArray as argument
// and computes two return values
//
// Subclass must implement:
//   template <class T>
//   void Calculate(double &, double &, int n, const T *data, const bool *mask)
#if MIRTK_Image_WITH_VTK
  #define mirtkCalculateVtkDataArray2() \
    static void Calculate(double &v1, double &v2, vtkDataArray *data, const bool *mask = NULL) \
    { \
      const void *p = data->GetVoidPointer(0); \
      const int   n = static_cast<int>(data->GetNumberOfTuples() * \
                                       data->GetNumberOfComponents()); \
      switch (data->GetDataType()) { \
        case VTK_SHORT:  Calculate(v1, v2, n, reinterpret_cast<const short  *>(p), mask); break; \
        case VTK_INT:    Calculate(v1, v2, n, reinterpret_cast<const int    *>(p), mask); break; \
        case VTK_FLOAT:  Calculate(v1, v2, n, reinterpret_cast<const float  *>(p), mask); break; \
        case VTK_DOUBLE: Calculate(v1, v2, n, reinterpret_cast<const double *>(p), mask); break; \
        default: \
          cerr << "Unsupported vtkDataArray type: " << data->GetDataType() << endl; \
          exit(1); \
      } \
    }
#else
  #define mirtkCalculateVtkDataArray2()
#endif


} } // namespace mirtk::data

#endif
