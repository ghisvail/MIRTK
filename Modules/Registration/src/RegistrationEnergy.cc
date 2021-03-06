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

#include "mirtk/RegistrationEnergy.h"

#include "mirtk/Math.h"
#include "mirtk/Memory.h"
#include "mirtk/Parallel.h"
#include "mirtk/Profiling.h"

#include "mirtk/FreeFormTransformation.h"
#include "mirtk/MultiLevelTransformation.h"

#include "mirtk/SparsityConstraint.h"


namespace mirtk {


// =============================================================================
// Auxiliary functor classes for parallel execution
// =============================================================================

// -----------------------------------------------------------------------------
/// Determine maximum norm of energy gradient
class MaxEnergyGradient
{
private:

  const FreeFormTransformation *_FFD;
  const double                 *_Gradient;
  double                        _MaxNorm;

public:

  /// Constructor
  MaxEnergyGradient(const FreeFormTransformation *ffd,
                    const double                 *gradient)
  :
    _FFD(ffd), _Gradient(gradient), _MaxNorm(.0)
  {}

  /// Copy constructor
  MaxEnergyGradient(const MaxEnergyGradient &other)
  :
    _FFD     (other._FFD),
    _Gradient(other._Gradient),
    _MaxNorm (other._MaxNorm)
  {}

  /// Split constructor
  MaxEnergyGradient(const MaxEnergyGradient &other, split)
  :
    _FFD     (other._FFD),
    _Gradient(other._Gradient),
    _MaxNorm (other._MaxNorm)
  {}

  /// Join results
  void join(const MaxEnergyGradient &other)
  {
    if (other._MaxNorm > _MaxNorm) _MaxNorm = other._MaxNorm;
  }

  /// Maximum norm
  double Norm() const { return sqrt(_MaxNorm); }

  /// Determine maximum norm of specified control point gradients
  void operator()(const blocked_range<int> &re)
  {
    double norm;
    int    x, y, z;

    for (int cp = re.begin(); cp != re.end(); ++cp) {
      _FFD->IndexToDOFs(cp, x, y, z);
      norm = pow(_Gradient[x], 2) + pow(_Gradient[y], 2) + pow(_Gradient[z], 2);
      if (norm > _MaxNorm) _MaxNorm = norm;
    }
  }
};

// -----------------------------------------------------------------------------
/// Normalize energy gradient
class NormalizeEnergyGradient
{
private:

  const FreeFormTransformation *_FFD;
  double                       *_Gradient;
  double                        _Sigma;

public:

  /// Constructor
  NormalizeEnergyGradient(const FreeFormTransformation *ffd,
                          double                       *gradient,
                          double                        sigma)
  :
    _FFD(ffd), _Gradient(gradient), _Sigma(sigma)
  {}

  /// Copy constructor
  NormalizeEnergyGradient(const NormalizeEnergyGradient &other)
  :
    _FFD     (other._FFD),
    _Gradient(other._Gradient),
    _Sigma   (other._Sigma)
  {}

  /// Normalize energy gradient
  void operator ()(const blocked_range<int> &re) const
  {
    double norm;
    int    x, y, z;

    for (int cp = re.begin(); cp != re.end(); ++cp) {
      _FFD->IndexToDOFs(cp, x, y, z);
      norm = pow(_Gradient[x], 2) + pow(_Gradient[y], 2) + pow(_Gradient[z], 2);
      if (norm) {
        norm = sqrt(norm) + _Sigma;
        _Gradient[x] /= norm;
        _Gradient[y] /= norm;
        _Gradient[z] /= norm;
      }
    }
  }
};

// =============================================================================
// Construction/Destruction
// =============================================================================

// -----------------------------------------------------------------------------
RegistrationEnergy::RegistrationEnergy()
:
  _Transformation    (NULL),
  _NormalizeGradients(false),
  _Preconditioning   (.0)
{
  // Bind broadcast method to energy term events
  _EventDelegate.Bind(LogEvent, MakeDelegate(this, &Observable::Broadcast));
}

// -----------------------------------------------------------------------------
RegistrationEnergy::~RegistrationEnergy()
{
  Clear();
}

// =============================================================================
// Energy terms
// =============================================================================

// -----------------------------------------------------------------------------
void RegistrationEnergy::Initialize()
{
  // Mark transformation as initially changed
  _Transformation->Changed(true);

  // Initialize energy terms
  for (size_t i = 0; i < _Term.size(); ++i) {
    _Term[i]->Transformation(_Transformation);
    _Term[i]->Initialize();
  }

  // Adjust weight of sparsity constraint
  SparsityConstraint *sparsity;
  int                 nsparsity = 0;
  for (size_t i = 0; i < _Term.size(); ++i) {
    if ((sparsity = dynamic_cast<SparsityConstraint *>(_Term[i]))) {
      const double weight = sparsity->Weight();
      if (weight == .0) continue;

      ++nsparsity;
      if (nsparsity > 1) {
        if (nsparsity == 2) {
          cerr << "WARNING Only first sparsity term will be used! Ignoring additional sparsity terms." << endl;
        }
        sparsity->Weight(.0);
        break;
      }

      // Update objective function inputs
      this->Update(true);

      // Compute initial objective function gradient
      // (excl. sparsity, non-normalized, non-conjugated)
      const int ndofs  = _Transformation->NumberOfDOFs();
      double *gradient = CAllocate<double>(ndofs);
      for (size_t j = 0; j < _Term.size(); ++j) {
        if (j != i) _Term[j]->Gradient(gradient, _StepLength);
      }

      // Compute weight normalization factor, i.e., norm of current gradient
      double norm    = .0;
      int    nactive = 0;
      for (int dof = 0; dof < ndofs; ++dof) {
        if (_Transformation->GetStatus(dof) == Active) {
          norm += abs(gradient[dof]);
          ++nactive;
        }
      }

      // Adjust weight
      if (nactive > 0) sparsity->Weight(weight * norm / nactive);

      // Free gradient vector
      Deallocate(gradient);
    }
  }
}

// -----------------------------------------------------------------------------
void RegistrationEnergy::Clear()
{
  for (size_t i = 0; i < _Term.size(); ++i) delete _Term[i];
  _Term.clear();
}

// -----------------------------------------------------------------------------
bool RegistrationEnergy::Empty() const
{
  return _Term.empty();
}

// -----------------------------------------------------------------------------
int RegistrationEnergy::NumberOfTerms() const
{
  return static_cast<int>(_Term.size());
}

// -----------------------------------------------------------------------------
int RegistrationEnergy::NumberOfActiveTerms() const
{
  int nactive = 0;
  for (size_t i = 0; i < _Term.size(); ++i) {
    if (_Term[i]->Weight() != .0) ++nactive;
  }
  return nactive;
}

// -----------------------------------------------------------------------------
void RegistrationEnergy::Add(EnergyTerm *term)
{
  term->AddObserver(_EventDelegate);
  _Term.push_back(term);
}

// -----------------------------------------------------------------------------
void RegistrationEnergy::Sub(EnergyTerm *term)
{
  Array<EnergyTerm *>::iterator it = _Term.begin();
  while (it != _Term.end()) {
    if (*it == term) {
      (*it)->DeleteObserver(_EventDelegate);
      _Term.erase(it);
      break;
    }
    ++it;
  }
}

// -----------------------------------------------------------------------------
EnergyTerm *RegistrationEnergy::Term(int i)
{
  return _Term[i];
}

// =============================================================================
// Parameters
// =============================================================================

// -----------------------------------------------------------------------------
bool RegistrationEnergy::Set(const char *name, const char *value)
{
  // Gradient normalization
  if (strncmp(name, "Normalize energy gradients", 26) == 0) {
    return FromString(value, _NormalizeGradients);
  } else if (strcmp(name, "Energy preconditioning") == 0) {
    return FromString(value, _Preconditioning);
  }
  // Default length of gradient approximation steps
  if (strcmp(name, "Length of steps")         == 0 ||
      strcmp(name, "Maximum length of steps") == 0) {
    return FromString(value, _StepLength) && _StepLength > .0;
  }
  // Energy term parameter
  bool known = false;
  for (size_t i = 0; i < _Term.size(); ++i) {
    known = _Term[i]->Set(name, value) || known;
  }
  return known;
}

// -----------------------------------------------------------------------------
ParameterList RegistrationEnergy::Parameter() const
{
  ParameterList params;
  for (size_t i = 0; i < _Term.size(); ++i) {
    Insert(params, _Term[i]->Parameter());
  }
  Insert(params, "Normalize energy gradients (experimental)", ToString(_NormalizeGradients));
  Insert(params, "Energy preconditioning",                    ToString(_Preconditioning));
  return params;
}

// =============================================================================
// Degrees of freedom
// =============================================================================

// -----------------------------------------------------------------------------
void RegistrationEnergy::Update(bool gradient)
{
  // If external update handler set, call it first so it can update the inputs
  // to the energy terms instead of having the energy term trigger the update.
  //
  // Attention: The external update handler has to make sure that the propagation
  //            of the Update call to the inputs of the energy terms is disabled
  //            in this case to avoid another unnecessary update of the input.
  //            E.g., RegisteredImage::SelfUpdate(false) for image similarities.
  if (_PreUpdateFunction) {
    MIRTK_START_TIMING();
    _PreUpdateFunction(gradient);
    MIRTK_DEBUG_TIMING(3, "preupdate of function");
  }

  // Propagate update to composite terms, which by default may trigger an
  // update of their input as well. However, it may be more efficient if an
  // external update handler updates all inputs at once. For example, all channels
  // of a multi-channel image, each channel being one input moving image of
  // an image similarity measure. In this case, the transformation and interpolation
  // domain for all channels is the same and they can be efficiently transformed
  // all together instead of each individually which involves duplicate inside
  // interpolation domain checks and coordinate transformations.
  //
  // Another way to make this more efficient could be to cache the transformed
  // voxel coordinates, i.e., for each output voxel the coordinate of the
  // respective input coordinate or NaN if outside the interpolation domain.
  // This cache should then be only updated once. However, how can you tell
  // which of the propagated RegisteredImage::Update calls is the first and
  // thus has to update the cache? This requires some modification and update
  // time stamps as they are used by the ITK pipeline. It probably is simpler
  // to just use an external update handler which has a reference to all the
  // input moving images and updates them all at once in predefined order.
  if (_Transformation->Changed() || gradient) {
    MIRTK_START_TIMING();
    for (size_t i = 0; i < _Term.size(); ++i) {
      if (_Term[i]->Weight() != .0) {
        _Term[i]->Update(gradient);
        _Term[i]->ResetValue(); // in case energy term does not do this
      }
    }
    // Mark transformation as unchanged
    _Transformation->Changed(false);
    MIRTK_DEBUG_TIMING(3, "update of energy function");
  }
}

// -----------------------------------------------------------------------------
bool RegistrationEnergy::Upgrade()
{
  bool changed = false;
  for (size_t i = 0; i < _Term.size(); ++i) {
    if (_Term[i]->Weight() != .0 && _Term[i]->Upgrade()) {
      _Term[i]->ResetValue();
      changed = true;
    }
  }
  return changed;
}

// -----------------------------------------------------------------------------
int RegistrationEnergy::NumberOfDOFs() const
{
  return _Transformation ? _Transformation->NumberOfDOFs() : 0;
}

// -----------------------------------------------------------------------------
void RegistrationEnergy::Put(const double *x)
{
  _Transformation->Put(x);
  _Transformation->Changed(true); // in case Put does not do this
  for (size_t i = 0; i < _Term.size(); ++i) {
    _Term[i]->ResetValue();
  }
}

// -----------------------------------------------------------------------------
void RegistrationEnergy::Get(double *x) const
{
  _Transformation->Get(x);
}

// -----------------------------------------------------------------------------
double RegistrationEnergy::Get(int dof) const
{
  return _Transformation->Get(dof);
}

// -----------------------------------------------------------------------------
double RegistrationEnergy::Step(double *dx)
{
  double max_delta = _Transformation->Update(dx);
  if (max_delta > .0) _Transformation->Changed(true); // in case Update does not do this
  for (size_t i = 0; i < _Term.size(); ++i) {
    _Term[i]->ResetValue();
  }
  return max_delta;
}

// =============================================================================
// Evaluation
// =============================================================================

// -----------------------------------------------------------------------------
double RegistrationEnergy::RawValue(int i)
{
  return (_Term[i]->Weight() != .0 ? _Term[i]->RawValue() : .0);
}

// -----------------------------------------------------------------------------
double RegistrationEnergy::InitialValue()
{
  MIRTK_START_TIMING();

  double value, sum = .0;
  for (size_t i = 0; i < _Term.size(); ++i) {
    value = (_Term[i]->Weight() != .0 ? _Term[i]->InitialValue() : .0);
    if (IsNaN(value)) {
      string name = _Term[i]->Name();
      if (name.empty()) name = ToString(i + 1);
      cerr << "RegistrationEnergy::InitialValue: Value of term " << name << " is NaN!" << endl;
      exit(1);
    }
    sum += value;
  }

  MIRTK_DEBUG_TIMING(3, "initial evaluation of energy function");
  return sum;
}

// -----------------------------------------------------------------------------
double RegistrationEnergy::InitialValue(int i)
{
  return _Term[i]->InitialValue();
}

// -----------------------------------------------------------------------------
double RegistrationEnergy::Value()
{
  MIRTK_START_TIMING();

  double value, sum = .0;
  for (size_t i = 0; i < _Term.size(); ++i) {
    value = (_Term[i]->Weight() != .0 ? _Term[i]->Value() : .0);
    if (IsNaN(value)) {
      string name = _Term[i]->Name();
      if (name.empty()) name = ToString(i + 1);
      cerr << "RegistrationEnergy::Value: Value of term " << name << " is NaN!" << endl;
      exit(1);
    }
    sum += value;
  }

  MIRTK_DEBUG_TIMING(3, "evaluation of energy function");
  return sum;
}

// -----------------------------------------------------------------------------
double RegistrationEnergy::Value(int i)
{
  return _Term[i]->Value();
}

// -----------------------------------------------------------------------------
void RegistrationEnergy::NormalizeGradient(double *gradient)
{
  const MultiLevelTransformation *mffd = NULL;
  const FreeFormTransformation   *affd = NULL;

  (mffd = dynamic_cast<const MultiLevelTransformation *>(_Transformation)) ||
  (affd = dynamic_cast<const FreeFormTransformation   *>(_Transformation));

  const int nlevels = (mffd ? mffd->NumberOfLevels() : (affd ? 1 : 0));
  if (nlevels == 0) return; // Skip if transformation is not a FFD

  MIRTK_START_TIMING();

  for (int lvl = 0; lvl < nlevels; ++lvl) {
    if (mffd) {
      if (!mffd->LocalTransformationIsActive(lvl)) continue;
      affd = mffd->GetLocalTransformation(lvl);
    }

    // Range of control point indices
    blocked_range<int> cps(0, affd->NumberOfCPs());

    // Determine maximum norm of control point gradients
    MaxEnergyGradient maximum(affd, gradient);
    parallel_reduce(cps, maximum);

    // Sigma value used to suppress noise
    const double sigma = _Preconditioning * maximum.Norm();

    // Normalize control point gradients to be possibly similar
    NormalizeEnergyGradient norm(affd, gradient, sigma);
    parallel_for(cps, norm);

    // Gradient w.r.t parameters of next active level
    gradient += affd->NumberOfDOFs();
  }

  MIRTK_DEBUG_TIMING(3, "normalization of energy gradient");
}

// -----------------------------------------------------------------------------
void RegistrationEnergy::Gradient(double *gradient, double step, bool *sgn_chg)
{
  MIRTK_START_TIMING();

  const int ndofs = _Transformation->NumberOfDOFs();
  SparsityConstraint *sparsity;

  // Use default step length if none specified
  if (step <= .0) step = _StepLength;

  // Initialize output variables
  memset(gradient, 0, ndofs * sizeof(double));
  if (sgn_chg) {
    for (int dof = 0; dof < ndofs; ++dof) {
      sgn_chg[dof] = true;
    }
  }

  // Sum (normalized) gradients of (weighted) energy term
  // excl. sparsity constraint which has to be added last,
  // such that it can determine whether or not the sparsity
  // gradient changes the sign of the energy gradient.
  if (_NormalizeGradients) {
    double w, W = .0;
    for (size_t i = 0; i < _Term.size(); ++i) {
      sparsity = dynamic_cast<SparsityConstraint *>(_Term[i]);
      if (sparsity) continue;
      W += abs(_Term[i]->Weight());
    }
    if (W == .0) {
      cerr << "RegistrationEnergy::Gradient: All energy terms have zero weight!" << endl;
      exit(1);
    }
    for (size_t i = 0; i < _Term.size(); ++i) {
      w = _Term[i]->Weight();
      if (w != .0) {
        sparsity = dynamic_cast<SparsityConstraint *>(_Term[i]);
        if (sparsity) continue;
        _Term[i]->Weight(w / W);
        _Term[i]->NormalizedGradient(gradient, step);
        _Term[i]->Weight(w);
      }
    }
  } else {
    for (size_t i = 0; i < _Term.size(); ++i) {
      if (_Term[i]->Weight() != .0) {
        sparsity = dynamic_cast<SparsityConstraint *>(_Term[i]);
        if (sparsity) continue;
        _Term[i]->Gradient(gradient, step);
      }
    }
  }

  // Add sparsity constraint gradient
  for (size_t i = 0; i < _Term.size(); ++i) {
    if (_Term[i]->Weight() != .0) {
      sparsity = dynamic_cast<SparsityConstraint *>(_Term[i]);
      if (sparsity) {
        sparsity->Gradient(gradient, step, sgn_chg);
        break; // Ignore additional sparsity terms
      }
    }
  }

  // Normalize energy gradient
  if (_Preconditioning > .0) this->NormalizeGradient(gradient);

  // Set gradient of passive DoFs to zero (deprecated)
  //
  // This is no longer enforced since IRTK ireg version 3.2 (MIRTK register v1.0)
  // which allows passive DoFs/CPs to be moved still by energy terms which
  // regularize the smoothness of the transformation. Such regularization may
  // propagate outwards from the image foreground to the boundary of the image domain.
  // The status of the DoFs should be taken into consideration by each individual
  // energy term, such that forces resulting from image dissimilarity measures
  // do not move passive DoFs/CPs.
#if 0
  for (int dof = 0; dof < ndofs; ++dof) {
    if (_Transformation->GetStatus(dof) == Passive) {
      gradient[dof] = .0;
    }
  }
#endif

  MIRTK_DEBUG_TIMING(3, "evaluation of energy gradient");
}

// -----------------------------------------------------------------------------
double RegistrationEnergy::GradientNorm(const double *dx) const
{
  return _Transformation->DOFGradientNorm(dx);
}

// -----------------------------------------------------------------------------
void RegistrationEnergy::GradientStep(const double *dx, double &min, double &max) const
{
  for (size_t i = 0; i < _Term.size(); ++i) {
    if (_Term[i]->Weight() != .0) {
      _Term[i]->GradientStep(dx, min, max);
    }
  }
}

// -----------------------------------------------------------------------------
double RegistrationEnergy::Evaluate(double *dx, double step, bool *sgn_chg)
{
  // Update energy function
  if (_Transformation->Changed()) this->Update(dx != NULL);

  // Evaluate gradient
  if (dx) this->Gradient(dx, step, sgn_chg);

  // Evaluate energy
  return this->Value();
}

// =============================================================================
// Debugging
// =============================================================================

// -----------------------------------------------------------------------------
void RegistrationEnergy::WriteDataSets(const char *prefix, const char *suffix, bool all) const
{
  for (size_t i = 0; i < _Term.size(); ++i) {
    if (_Term[i]->Weight() != .0) {
      _Term[i]->WriteDataSets(prefix, suffix, all);
    }
  }
}

// -----------------------------------------------------------------------------
void RegistrationEnergy::WriteGradient(const char *prefix, const char *suffix) const
{
  for (size_t i = 0; i < _Term.size(); ++i) {
    if (_Term[i]->Weight() != .0) {
      _Term[i]->WriteGradient(prefix, suffix);
    }
  }
}


} // namespace mirtk
