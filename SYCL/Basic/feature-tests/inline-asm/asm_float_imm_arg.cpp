// UNSUPPORTED: cuda
// REQUIRES: gpu,linux
// RUN: %clangxx -fsycl %s -DINLINE_ASM -o %t.out
// RUN: %t.out
// RUN: %clangxx -fsycl %s -o %t.ref.out
// RUN: %t.ref.out

#include "include/asmhelper.h"
#include <CL/sycl.hpp>
#include <cmath>
#include <iostream>
#include <vector>

constexpr double IMM_ARGUMENT = 0.5;
using dataType = cl::sycl::cl_double;

template <typename T = dataType>
struct KernelFunctor : WithInputBuffers<T, 1>, WithOutputBuffer<T> {
  KernelFunctor(const std::vector<T> &input) : WithInputBuffers<T, 1>(input), WithOutputBuffer<T>(input.size()) {}

  void operator()(cl::sycl::handler &cgh) {
    auto A = this->getInputBuffer(0).template get_access<cl::sycl::access::mode::read>(cgh);
    auto B = this->getOutputBuffer().template get_access<cl::sycl::access::mode::write>(cgh);

    cgh.parallel_for<KernelFunctor<T>>(
        cl::sycl::range<1>{this->getOutputBufferSize()}, [=](cl::sycl::id<1> wiID) [[cl::intel_reqd_sub_group_size(8)]] {
#if defined(INLINE_ASM) && defined(__SYCL_DEVICE_ONLY__)
          asm("mul (M1, 8) %0(0, 0)<1> %1(0, 0)<1;1,0> %2"
              : "=rw"(B[wiID])
              : "rw"(A[wiID]), "rw"(IMM_ARGUMENT));
#else
          B[wiID] = A[wiID] * IMM_ARGUMENT;
#endif
        });
  }
};

int main() {
  std::vector<dataType> input(DEFAULT_PROBLEM_SIZE);
  for (int i = 0; i < DEFAULT_PROBLEM_SIZE; i++)
    input[i] = (double)1 / std::pow(2, i);

  KernelFunctor<> f(input);
  if (!launchInlineASMTest(f))
    return 0;

  auto &B = f.getOutputBufferData();
  for (int i = 0; i < DEFAULT_PROBLEM_SIZE; ++i) {
    if (B[i] != input[i] * IMM_ARGUMENT) {
      std::cerr << "At index: " << i << ". ";
      std::cerr << B[i] << " != " << input[i] * IMM_ARGUMENT << "\n";
      return 1;
    }
  }
  return 0;
}
