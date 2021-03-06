/*
 * Medical Image Registration ToolKit (MIRTK)
 *
 * Copyright 2008-2015 Imperial College London
 * Copyright 2008-2013 Daniel Rueckert, Julia Schnabel
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

#include "mirtk/Common.h"
#include "mirtk/Options.h"

#include "mirtk/IOConfig.h"

#include "mirtk/Dilation.h"
#include "mirtk/Erosion.h"

using namespace mirtk;


// =============================================================================
// Help
// =============================================================================

// -----------------------------------------------------------------------------
void PrintHelp(const char *name)
{
  cout << "\n";
  cout << "Usage: " << name << " <input> <output> [options]\n";
  cout << "\n";
  cout << "Description:\n";
  cout << "  Closes holes by first dilating and then eroding an input image.\n";
  cout << "\n";
  cout << "Arguments:\n";
  cout << "  input    Input intensity/segmentation image.\n";
  cout << "  output   Closed output image.\n";
  cout << "\n";
  cout << "Optional arguments:\n";
  cout << "  -iterations <n>     Number of dilation/erosion iterations. (default: 1)\n";
  cout << "  -connectivity <n>   Type of voxel connectivity (4, 6, 18, or 26). (default: 26)\n";
  PrintStandardOptions(cout);
  cout << "\n";
  cout.flush();
}

// =============================================================================
// Auxiliaries
// =============================================================================

// -----------------------------------------------------------------------------
template <class TVoxel>
void Close(BaseImage *image, int iterations, ConnectivityType connectivity)
{
  int i1, j1, k1, i2, j2, k2;
  image->PutBackgroundValueAsDouble(.0);
  image->BoundingBox(i1, j1, k1, i2, j2, k2);

  i1 -= iterations;
  j1 -= iterations;
  k1 -= iterations;
  i2 += iterations;
  j2 += iterations;
  k2 += iterations;

  if (i1 < 0 || i2 >= image->X() ||
      j1 < 0 || j2 >= image->Y() ||
      k1 < 0 || k2 >= image->Z()) {

    GenericImage<TVoxel> padded(i2 - i1 + 1, j2 - j1 + 1, k2 - k1 + 1);

    i1 += iterations;
    j1 += iterations;
    k1 += iterations;
    i2 -= iterations;
    j2 -= iterations;
    k2 -= iterations;

    for (int k = k1; k <= k2; ++k)
    for (int j = j1; j <= j2; ++j)
    for (int i = i1; i <= i2; ++i) {
      padded(i - i1 + iterations, j - j1 + iterations, k - k1 + iterations)
          = static_cast<TVoxel>(image->GetAsDouble(i, j, k));
    }

    Dilate<TVoxel>(&padded, iterations, connectivity);
    Erode <TVoxel>(&padded, iterations, connectivity);

    for (int k = k1; k <= k2; ++k)
    for (int j = j1; j <= j2; ++j)
    for (int i = i1; i <= i2; ++i) {
      image->PutAsDouble(i, j, k, static_cast<double>(padded(i - i1 + iterations,
                                                             j - j1 + iterations,
                                                             k - k1 + iterations)));
    }

  } else {

    Dilate<TVoxel>(image, iterations, connectivity);
    Erode <TVoxel>(image, iterations, connectivity);

  }
}

// =============================================================================
// Main
// =============================================================================

// -----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
  EXPECTS_POSARGS(2);

  const char *input_name  = POSARG(1);
  const char *output_name = POSARG(2);

  int              iterations   = 1;
  ConnectivityType connectivity = CONNECTIVITY_26;

  for (ALL_OPTIONS) {
    if (OPTION("-iterations") || OPTION("-iter")) {
      PARSE_ARGUMENT(iterations);
    }
    else if (OPTION("-connectivity") || OPTION("-neighbors") || OPTION("-number-of-neighbors")) {
      PARSE_ARGUMENT(connectivity);
    }
    else HANDLE_STANDARD_OR_UNKNOWN_OPTION();
  }

  InitializeIOLibrary();
  unique_ptr<BaseImage> image(BaseImage::New(input_name));

  if (verbose) cout << "Closing ... ", cout.flush();
  switch (image->GetDataType()) {
    case MIRTK_VOXEL_BINARY:  Close<BinaryPixel>(image.get(), iterations, connectivity); break;
    case MIRTK_VOXEL_GREY:    Close<GreyPixel  >(image.get(), iterations, connectivity); break;
    case MIRTK_VOXEL_REAL:    Close<RealPixel  >(image.get(), iterations, connectivity); break;
    default: {
      RealImage other(*image);
      Close<RealPixel>(&other, iterations, connectivity);
      *image = other;
    } break;
  }
  if (verbose) cout << "done" << endl;

  image->Write(output_name);

  return 0;
}

