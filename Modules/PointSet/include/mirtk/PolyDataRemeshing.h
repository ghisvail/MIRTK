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

#ifndef MIRTK_PolyDataRemeshing_H
#define MIRTK_PolyDataRemeshing_H

#include "mirtk/PolyDataFilter.h"

#include "mirtk/Point.h"

#include "vtkPriorityQueue.h"

class vtkCellArray;
class vtkIdList;
class vtkPoints;


namespace mirtk {


class Transformation;


/**
 * Adaptive local remeshing of triangulated surface mesh
 *
 * Park et al., A non-self-intersecting adaptive deformable surface for
 * complex boundary extraction from volumetric images, 25, 421–440 (2001).
 *
 * \todo Interpolate cell data during remeshing. The current implementation only
 *       preserves and interpolates point data arrays. Cell attributes are discarded.
 */
class PolyDataRemeshing : public PolyDataFilter
{
  mirtkObjectMacro(PolyDataRemeshing);

  // ---------------------------------------------------------------------------
  // Types

public:

  /// Enumeration of cell order in which melting is performed
  enum Order
  {
    INDEX,        ///< Cell index
    AREA,         ///< Cell area
    SHORTEST_EDGE ///< Length of shortest edge
  };

protected:

  // ---------------------------------------------------------------------------
  // Attributes

  /// Triangulated input mesh
  mirtkAttributeMacro(vtkSmartPointer<vtkPolyData>, TriangulatedInput);

  /// Optional input transformation used to determine edge length and triangle area
  mirtkPublicAggregateMacro(const class Transformation, Transformation);

  /// Output point labels
  mirtkAttributeMacro(vtkSmartPointer<vtkDataArray>, OutputPointLabels);

  /// Minimum angle between edge end point normals to consider the edge as
  /// an important feature edge which is excluded from any melting operation.
  mirtkPublicAttributeMacro(double, MinFeatureAngle);
  double _MinFeatureAngleCos; ///< 1 - cos(_MinFeatureAngle)

  /// If edge end point normals make up an angle greater than this maximum
  /// feature angle, the respective edge is subdivided even if the edge is
  /// shorter than the _MaxEdgeLength if both edges resulting from splitting
  /// the edge in half are at least _MinEdgeLength long.
  mirtkPublicAttributeMacro(double, MaxFeatureAngle);
  double _MaxFeatureAngleCos; ///< 1 - cos(_MaxFeatureAngle)

  /// Minimum edge length
  mirtkPublicAttributeMacro(double, MinEdgeLength);
  double _MinEdgeLengthSquared;

  /// Maximum edge length
  mirtkPublicAttributeMacro(double, MaxEdgeLength);
  double _MaxEdgeLengthSquared;

  /// Point data array used to adapt the edge length range for each node
  ///
  /// The scalar point data values are rescaled linearly to [0, 1] after
  /// clamping the point data range to the 5th and 95th percentile range.
  /// The rescaled value is then plugged into a logistic function which
  /// determines the linear interpolation weights of the global
  /// _MinEdgeLength and _MaxEdgeLength range. This obtains an individual
  /// edge length range for each point. The desired edge length range of a
  /// given edge is then the mean of the minimum/maximum edge length of the
  /// two end points of the edge.
  mirtkPublicAttributeMacro(vtkSmartPointer<vtkDataArray>, AdaptiveEdgeLengthArray);

  /// Per-cell minimum edge length
  mirtkPublicAttributeMacro(vtkSmartPointer<vtkDataArray>, MinCellEdgeLengthArray);

  /// Per-cell maximum edge length
  mirtkPublicAttributeMacro(vtkSmartPointer<vtkDataArray>, MaxCellEdgeLengthArray);

  /// Per-node minimum edge length
  mirtkAttributeMacro(vtkSmartPointer<vtkDataArray>, MinEdgeLengthArray);

  /// Per-node maximum edge length
  mirtkAttributeMacro(vtkSmartPointer<vtkDataArray>, MaxEdgeLengthArray);

  /// Define in which order to process the cells in the melting pass
  mirtkPublicAttributeMacro(Order, MeltingOrder);

  /// Priority queue used by melting pass
  mirtkAttributeMacro(vtkSmartPointer<vtkPriorityQueue>, MeltingQueue);

  /// Whether to melt nodes with connectivity three by merging the adjacent triangles
  mirtkPublicAttributeMacro(bool, MeltNodes);

  /// Whether to melt entire triangles if all three edges are below threshold
  mirtkPublicAttributeMacro(bool, MeltTriangles);

  /// Invert pairs of triangles which share an edge that is longer than the maximum
  mirtkPublicAttributeMacro(bool, InvertTrianglesSharingOneLongEdge);

  /// Invert edge of two triangles when it increases the minimum height
  mirtkPublicAttributeMacro(bool, InvertTrianglesToIncreaseMinHeight);

  /// Number of melted nodes with connectivity 3
  mirtkReadOnlyAttributeMacro(int, NumberOfMeltedNodes);

  /// Number of melted edges
  mirtkReadOnlyAttributeMacro(int, NumberOfMeltedEdges);

  /// Number of melted triangles
  mirtkReadOnlyAttributeMacro(int, NumberOfMeltedCells);

  /// Number of edge inversions
  mirtkReadOnlyAttributeMacro(int, NumberOfInversions);

  /// Number of bisections
  mirtkReadOnlyAttributeMacro(int, NumberOfBisections);

  /// Number of trisections
  mirtkReadOnlyAttributeMacro(int, NumberOfTrisections);

  /// Number of quadsections
  mirtkReadOnlyAttributeMacro(int, NumberOfQuadsections);

  /// Copy attributes of this class from another instance
  void CopyAttributes(const PolyDataRemeshing &);

public:

  /// Number of melting operations
  int NumberOfMeltings() const;

  /// Number of subdivision operations
  int NumberOfSubdivisions() const;

  /// Number of local remeshing operations
  int NumberOfChanges() const;

  // ---------------------------------------------------------------------------
  // Construction/Destruction

  /// Default constructor
  PolyDataRemeshing();

  /// Copy constructor
  PolyDataRemeshing(const PolyDataRemeshing &);

  /// Assignment operator
  PolyDataRemeshing &operator =(const PolyDataRemeshing &);

  /// Destructor
  virtual ~PolyDataRemeshing();

  // ---------------------------------------------------------------------------
  // Inline auxiliary functions

private:

  /// Get (transformed) surface point
  void GetPoint(vtkIdType, double [3]) const;

  /// Get (transformed) surface point normal
  void GetNormal(vtkIdType, double [3]) const;

  /// Calculate area of (transformed) triangle
  double ComputeArea(vtkIdType) const;

  /// Get (non-transformed) edge middle point
  void MiddlePoint(vtkIdType, vtkIdType, double [3]) const;

  /// Get (non-transformed) edge middle point
  Point MiddlePoint(vtkIdType, vtkIdType) const;

  /// Get node connectivity
  int NodeConnectivity(vtkIdType) const;

  /// Mark cell as deleted and remove all references to it
  void DeleteCell(vtkIdType);

  /// Mark cells as deleted and remove all references to them
  void DeleteCells(vtkIdList *);

  /// Replace cell point, also updating point to cell references
  void ReplaceCellPoint(vtkIdType, vtkIdType, vtkIdType);

  /// Get neighboring triangle sharing an edge with specified triangle
  vtkIdType GetCellEdgeNeighbor(vtkIdType, vtkIdType, vtkIdType) const;

  /// Get other vertices of cell edge neighbors
  void GetCellPointNeighbors(vtkIdType, vtkIdType, vtkIdList *) const;

  /// Check connectivity of edge neighbors
  ///
  /// If other vertex of edge neighbor triangle has connectivity three,
  /// the three triangles adjacent to it are replaced by the union of these
  /// triangle and the melting operation can be performed. Otherwise, if
  /// more than one node with connectivity three is found which is adjacent
  /// to the edge corners, melting the (triangle) edge would cause degeneration
  /// of adjacent triangles.
  ///
  /// @return ID of other vertex of edge neighbor or -1 if not unique or if
  ///         its connectivity prohibits a melting operation.
  vtkIdType GetCellEdgeNeighborPoint(vtkIdType, vtkIdType, vtkIdType, bool = false);

  /// Get priority of cell during melting pass
  double MeltingPriority(vtkIdType) const;

  /// Interpolate point attributes when subdividing edge
  void InterpolatePointData(vtkIdType, vtkIdType, vtkIdType);

  /// Interpolate point attributes when melting triangle
  void InterpolatePointData(vtkIdType, vtkIdList *, double *);

  /// Get squared minimum length for specified edge
  double SquaredMinEdgeLength(vtkIdType, vtkIdType) const;

  /// Get squared maximum length for specified edge
  double SquaredMaxEdgeLength(vtkIdType, vtkIdType) const;

  // ---------------------------------------------------------------------------
  // Local remeshing operations
protected:

  /// Collapse single short edge of two adjacent triangles
  bool MeltEdge(vtkIdType, vtkIdType, vtkIdType, vtkIdList *);

  /// Collapse entire triangle with more than one too short edges
  bool MeltTriangle(vtkIdType, vtkIdList *);

  /// Melt edges or triangle if one or more edge is too short
  void MeltingOfCells();

  /// Replace triangles adjacent to node with connectivity three by single triangle
  void MeltingOfNodes();

  /// Invert triangles which share one too long edge
  void InversionOfTrianglesSharingOneLongEdge();

  /// Invert triangles when minimum height over this edge increases
  void InversionOfTrianglesToIncreaseMinHeight();

  /// Bisect triangle
  void Bisect(vtkIdType, vtkIdType, vtkIdType, vtkIdType, vtkPoints *, vtkCellArray *);

  /// Trisect triangle
  void Trisect(vtkIdType, vtkIdType, vtkIdType, vtkIdType, vtkPoints *, vtkCellArray *);

  /// Quadsect triangle
  void Quadsect(vtkIdType, vtkIdType, vtkIdType, vtkIdType, vtkPoints *, vtkCellArray *);

  // ---------------------------------------------------------------------------
  // Execution

  /// Initialize filter after input and parameters are set
  virtual void Initialize();

  /// Initialize edge length range for each node
  void InitializeEdgeLengthRange();

  /// Perform local remeshing passes
  virtual void Execute();

  /// Perform first pass: melt edges or triangles if one or more edges are too short
  void Melting();

  /// Perform second pass: inversion of triangles sharing a long edge
  void Inversion();

  /// Perform third pass: subdivide triangles with remaining long edges
  void Subdivision();

  /// Finalize filter execution
  virtual void Finalize();

  // ---------------------------------------------------------------------------
  // Alternative VTK-like API

public:

  /// Enable/disable melting of nodes with connectivity three
  mirtkOnOffMacro(MeltNodes);

  /// Enable/disable melting of triangles when all edges are too short
  mirtkOnOffMacro(MeltTriangles);

  /// Enable/disable inversion of triangles which share one long edge
  mirtkOnOffMacro(InvertTrianglesSharingOneLongEdge);

  /// Enable/disable inversion of triangles when it increases minimum height
  mirtkOnOffMacro(InvertTrianglesToIncreaseMinHeight);

};

////////////////////////////////////////////////////////////////////////////////
// Inline definitions
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
int PolyDataRemeshing::NumberOfMeltings() const
{
  return _NumberOfMeltedNodes + _NumberOfMeltedEdges + _NumberOfMeltedCells;
}

// -----------------------------------------------------------------------------
int PolyDataRemeshing::NumberOfSubdivisions() const
{
  return _NumberOfBisections + _NumberOfTrisections + _NumberOfQuadsections;
}

// -----------------------------------------------------------------------------
int PolyDataRemeshing::NumberOfChanges() const
{
  return NumberOfMeltings() + NumberOfInversions() + NumberOfSubdivisions();
}

} // namespace mirtk

#endif // MIRTK_PolyDataRemeshing_H
