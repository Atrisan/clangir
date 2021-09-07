// RUN: %clangxx -fsycl -fsycl-targets=%sycl_triple %s -I . -o %t.out
// RUN: %HOST_RUN_PLACEHOLDER %t.out
// RUN: %CPU_RUN_PLACEHOLDER %t.out
// RUN: %GPU_RUN_PLACEHOLDER %t.out
// RUN: %ACC_RUN_PLACEHOLDER %t.out

// TODO: enable compile+runtime checks for operations defined in SPIR-V 1.3.
// That requires either adding a switch to clang (-spirv-max-version=1.3) or
// raising the spirv version from 1.1. to 1.3 for spirv translator
// unconditionally. Using operators specific for spirv 1.3 and higher with
// -spirv-max-version=1.1 being set by default causes assert/check fails
// in spirv translator.
// RUNx: %clangxx -fsycl -fsycl-targets=%sycl_triple -DSPIRV_1_3 %s -I . -o \
   %t13.out

#include "support.h"
#include <CL/sycl.hpp>
#include <algorithm>
#include <cassert>
#include <limits>
#include <numeric>
#include <vector>
using namespace sycl;
using namespace sycl::ext::oneapi;

template <class SpecializationKernelName, int TestNumber>
class exclusive_scan_kernel;

// std::exclusive_scan isn't implemented yet, so use serial implementation
// instead
namespace emu {
template <typename InputIterator, typename OutputIterator,
          class BinaryOperation, typename T>
OutputIterator exclusive_scan(InputIterator first, InputIterator last,
                              OutputIterator result, T init,
                              BinaryOperation binary_op) {
  T partial = init;
  for (InputIterator it = first; it != last; ++it) {
    *(result++) = partial;
    partial = binary_op(partial, *it);
  }
  return result;
}
} // namespace emu

template <typename SpecializationKernelName, typename InputContainer,
          typename OutputContainer, class BinaryOperation>
void test(queue q, InputContainer input, OutputContainer output,
          BinaryOperation binary_op,
          typename OutputContainer::value_type identity) {
  typedef typename InputContainer::value_type InputT;
  typedef typename OutputContainer::value_type OutputT;
  typedef class exclusive_scan_kernel<SpecializationKernelName, 0> kernel_name0;
  typedef class exclusive_scan_kernel<SpecializationKernelName, 1> kernel_name1;
  typedef class exclusive_scan_kernel<SpecializationKernelName, 2> kernel_name2;
  typedef class exclusive_scan_kernel<SpecializationKernelName, 3> kernel_name3;
  OutputT init = 42;
  size_t N = input.size();
  size_t G = 64;
  std::vector<OutputT> expected(N);
  {
    buffer<InputT> in_buf(input.data(), input.size());
    buffer<OutputT> out_buf(output.data(), output.size());
    q.submit([&](handler &cgh) {
      auto in = in_buf.template get_access<access::mode::read>(cgh);
      auto out = out_buf.template get_access<access::mode::discard_write>(cgh);
      cgh.parallel_for<kernel_name0>(nd_range<1>(G, G), [=](nd_item<1> it) {
        group<1> g = it.get_group();
        int lid = it.get_local_id(0);
        out[lid] = exclusive_scan(g, in[lid], binary_op);
      });
    });
  }
  emu::exclusive_scan(input.begin(), input.begin() + G, expected.begin(),
                      identity, binary_op);
  assert(std::equal(output.begin(), output.begin() + G, expected.begin()));

  {
    buffer<InputT> in_buf(input.data(), input.size());
    buffer<OutputT> out_buf(output.data(), output.size());
    q.submit([&](handler &cgh) {
      auto in = in_buf.template get_access<access::mode::read>(cgh);
      auto out = out_buf.template get_access<access::mode::discard_write>(cgh);
      cgh.parallel_for<kernel_name1>(nd_range<1>(G, G), [=](nd_item<1> it) {
        group<1> g = it.get_group();
        int lid = it.get_local_id(0);
        out[lid] = exclusive_scan(g, in[lid], init, binary_op);
      });
    });
  }
  emu::exclusive_scan(input.begin(), input.begin() + G, expected.begin(), init,
                      binary_op);
  assert(std::equal(output.begin(), output.begin() + G, expected.begin()));

  {
    buffer<InputT> in_buf(input.data(), input.size());
    buffer<OutputT> out_buf(output.data(), output.size());
    q.submit([&](handler &cgh) {
      auto in = in_buf.template get_access<access::mode::read>(cgh);
      auto out = out_buf.template get_access<access::mode::discard_write>(cgh);
      cgh.parallel_for<kernel_name2>(nd_range<1>(G, G), [=](nd_item<1> it) {
        group<1> g = it.get_group();
        exclusive_scan(g, in.get_pointer(), in.get_pointer() + N,
                       out.get_pointer(), binary_op);
      });
    });
  }
  emu::exclusive_scan(input.begin(), input.begin() + N, expected.begin(),
                      identity, binary_op);
  assert(std::equal(output.begin(), output.begin() + N, expected.begin()));

  {
    buffer<InputT> in_buf(input.data(), input.size());
    buffer<OutputT> out_buf(output.data(), output.size());
    q.submit([&](handler &cgh) {
      auto in = in_buf.template get_access<access::mode::read>(cgh);
      auto out = out_buf.template get_access<access::mode::discard_write>(cgh);
      cgh.parallel_for<kernel_name3>(nd_range<1>(G, G), [=](nd_item<1> it) {
        group<1> g = it.get_group();
        exclusive_scan(g, in.get_pointer(), in.get_pointer() + N,
                       out.get_pointer(), init, binary_op);
      });
    });
  }
  emu::exclusive_scan(input.begin(), input.begin() + N, expected.begin(), init,
                      binary_op);
  assert(std::equal(output.begin(), output.begin() + N, expected.begin()));
}

int main() {
  queue q;
  if (!isSupportedDevice(q.get_device())) {
    std::cout << "Skipping test\n";
    return 0;
  }

  constexpr int N = 128;
  std::array<int, N> input;
  std::array<int, N> output;
  std::iota(input.begin(), input.end(), 0);
  std::fill(output.begin(), output.end(), 0);

  test<class KernelNamePlusV>(q, input, output, ext::oneapi::plus<>(), 0);
  test<class KernelNameMinimumV>(q, input, output, ext::oneapi::minimum<>(),
                                 std::numeric_limits<int>::max());
  test<class KernelNameMaximumV>(q, input, output, ext::oneapi::maximum<>(),
                                 std::numeric_limits<int>::lowest());

  test<class KernelNamePlusI>(q, input, output, ext::oneapi::plus<int>(), 0);
  test<class KernelNameMinimumI>(q, input, output, ext::oneapi::minimum<int>(),
                                 std::numeric_limits<int>::max());
  test<class KernelNameMaximumI>(q, input, output, ext::oneapi::maximum<int>(),
                                 std::numeric_limits<int>::lowest());

#ifdef SPIRV_1_3
  test<class KernelName_VzAPutpBRRJrQPB>(q, input, output,
                                         ext::oneapi::multiplies<int>(), 1);
  test<class KernelName_UXdGbr>(q, input, output, ext::oneapi::bit_or<int>(),
                                0);
  test<class KernelName_saYaodNyJknrPW>(q, input, output,
                                        ext::oneapi::bit_xor<int>(), 0);
  test<class KernelName_GPcuAlvAOjrDyP>(q, input, output,
                                        ext::oneapi::bit_and<int>(), ~0);
#endif // SPIRV_1_3

  std::cout << "Test passed." << std::endl;
}
