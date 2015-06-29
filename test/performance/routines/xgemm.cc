
// =================================================================================================
// This file is part of the CLBlast project. The project is licensed under Apache Version 2.0. This
// project loosely follows the Google C++ styleguide and uses a tab-size of two spaces and a max-
// width of 100 characters per line.
//
// Author(s):
//   Cedric Nugteren <www.cedricnugteren.nl>
//
// This file implements the Xgemm command-line interface performance tester.
//
// =================================================================================================

#include "performance/client.h"
#include "routines/xgemm.h"

// =================================================================================================

// Main function (not within the clblast namespace)
int main(int argc, char *argv[]) {
  switch(clblast::GetPrecision(argc, argv)) {
    case clblast::Precision::kHalf:
      throw std::runtime_error("Unsupported precision mode");
    case clblast::Precision::kSingle:
      clblast::RunClient<clblast::TestXgemm<float>, float>(argc, argv); break;
    case clblast::Precision::kDouble:
      clblast::RunClient<clblast::TestXgemm<double>, double>(argc, argv); break;
    case clblast::Precision::kComplexSingle:
      clblast::RunClient<clblast::TestXgemm<clblast::float2>, clblast::float2>(argc, argv); break;
    case clblast::Precision::kComplexDouble:
      clblast::RunClient<clblast::TestXgemm<clblast::double2>, clblast::double2>(argc, argv); break;
  }
  return 0;
}

// =================================================================================================