
// =================================================================================================
// This file is part of the CLBlast project. The project is licensed under Apache Version 2.0. This
// project loosely follows the Google C++ styleguide and uses a tab-size of two spaces and a max-
// width of 100 characters per line.
//
// Author(s):
//   Cedric Nugteren <www.cedricnugteren.nl>
//
// This file implements the Routine base class (see the header for information about the class).
//
// =================================================================================================

#include <string>
#include <vector>
#include <chrono>
#include <cstdlib>

#include "routine.hpp"

namespace clblast {
// =================================================================================================

// The constructor does all heavy work, errors are returned as exceptions
Routine::Routine(Queue &queue, EventPointer event, const std::string &name,
                 const std::vector<std::string> &kernel_names, const Precision precision,
                 const std::vector<const Database::DatabaseEntry*> &userDatabase,
                 std::initializer_list<const char *> source):
    precision_(precision),
    routine_name_(name),
    kernel_names_(kernel_names),
    queue_(queue),
    event_(event),
    context_(queue_.GetContext()),
    device_(queue_.GetDevice()),
    device_name_(device_.Name()),
    db_(kernel_names) {

  InitDatabase(userDatabase);
  InitProgram(source);
}

void Routine::InitDatabase(const std::vector<const Database::DatabaseEntry*> &userDatabase) {
  for (const auto &kernel_name : kernel_names_) {

    // Queries the cache to see whether or not the kernel parameter database is already there
    bool has_db;
    db_(kernel_name) = DatabaseCache::Instance().Get(DatabaseKeyRef{ precision_, device_name_, kernel_name },
                                                     &has_db);
    if (has_db) { continue; }

    // Builds the parameter database for this device and routine set and stores it in the cache
    db_(kernel_name) = Database(device_, kernel_name, precision_, userDatabase);
    DatabaseCache::Instance().Store(DatabaseKey{ precision_, device_name_, kernel_name },
                                    Database{ db_(kernel_name) });
  }
}

void Routine::InitProgram(std::initializer_list<const char *> source) {

  // Queries the cache to see whether or not the program (context-specific) is already there
  bool has_program;
  program_ = ProgramCache::Instance().Get(ProgramKeyRef{ context_(), precision_, routine_name_ },
                                          &has_program);
  if (has_program) { return; }

  // Sets the build options from an environmental variable (if set)
  auto options = std::vector<std::string>();
  const auto environment_variable = std::getenv("CLBLAST_BUILD_OPTIONS");
  if (environment_variable != nullptr) {
    options.push_back(std::string(environment_variable));
  }

  // Queries the cache to see whether or not the binary (device-specific) is already there. If it
  // is, a program is created and stored in the cache
  bool has_binary;
  auto binary = BinaryCache::Instance().Get(BinaryKeyRef{ precision_, routine_name_, device_name_ },
                                            &has_binary);
  if (has_binary) {
    program_ = Program(device_, context_, binary);
    program_.Build(device_, options);
    ProgramCache::Instance().Store(ProgramKey{ context_(), precision_, routine_name_ },
                                   Program{ program_ });
    return;
  }

  // Otherwise, the kernel will be compiled and program will be built. Both the binary and the
  // program will be added to the cache.

  // Inspects whether or not cl_khr_fp64 is supported in case of double precision
  if ((precision_ == Precision::kDouble && !PrecisionSupported<double>(device_)) ||
      (precision_ == Precision::kComplexDouble && !PrecisionSupported<double2>(device_))) {
    throw RuntimeErrorCode(StatusCode::kNoDoublePrecision);
  }

  // As above, but for cl_khr_fp16 (half precision)
  if (precision_ == Precision::kHalf && !PrecisionSupported<half>(device_)) {
    throw RuntimeErrorCode(StatusCode::kNoHalfPrecision);
  }

  // Collects the parameters for this device in the form of defines, and adds the precision
  auto source_string = std::string{""};
  for (const auto &kernel_name : kernel_names_) {
    source_string += db_(kernel_name).GetDefines();
  }
  source_string += "#define PRECISION "+ToString(static_cast<int>(precision_))+"\n";

  // Adds the name of the routine as a define
  source_string += "#define ROUTINE_"+routine_name_+"\n";

  // For specific devices, use the non-IEE754 compilant OpenCL mad() instruction. This can improve
  // performance, but might result in a reduced accuracy.
  if (device_.IsAMD() && device_.IsGPU()) {
    source_string += "#define USE_CL_MAD 1\n";
  }

  // For specific devices, use staggered/shuffled workgroup indices.
  if (device_.IsAMD() && device_.IsGPU()) {
    source_string += "#define USE_STAGGERED_INDICES 1\n";
  }

  // For specific devices add a global synchronisation barrier to the GEMM kernel to optimize
  // performance through better cache behaviour
  if (device_.IsARM() && device_.IsGPU()) {
    source_string += "#define GLOBAL_MEM_FENCE 1\n";
  }

  // Loads the common header (typedefs and defines and such)
  source_string +=
    #include "kernels/common.opencl"
  ;

  // Adds routine-specific code to the constructed source string
  for (const char *s: source) {
    source_string += s;
  }

  // Prints details of the routine to compile in case of debugging in verbose mode
  #ifdef VERBOSE
    printf("[DEBUG] Compiling routine '%s-%s' for device '%s'\n",
           routine_name_.c_str(), ToString(precision_).c_str(), device_name_.c_str());
    const auto start_time = std::chrono::steady_clock::now();
  #endif

  // Compiles the kernel
  program_ = Program(context_, source_string);
  try {
    program_.Build(device_, options);
  } catch (const CLError &e) {
    if (e.status() == CL_BUILD_PROGRAM_FAILURE) {
      fprintf(stdout, "OpenCL compiler error/warning: %s\n",
              program_.GetBuildInfo(device_).c_str());
    }
    throw;
  }

  // Store the compiled binary and program in the cache
  BinaryCache::Instance().Store(BinaryKey{ precision_, routine_name_, device_name_ },
                                program_.GetIR());

  ProgramCache::Instance().Store(ProgramKey{ context_(), precision_, routine_name_ },
                                 Program{ program_ });

  // Prints the elapsed compilation time in case of debugging in verbose mode
  #ifdef VERBOSE
    const auto elapsed_time = std::chrono::steady_clock::now() - start_time;
    const auto timing = std::chrono::duration<double,std::milli>(elapsed_time).count();
    printf("[DEBUG] Completed compilation in %.2lf ms\n", timing);
  #endif
}

// =================================================================================================
} // namespace clblast
