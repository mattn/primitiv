#include <primitiv/config.h>

#include <primitiv/cuda16_device.h>
#include <primitiv/internal/cuda_utils.h>
#include <primitiv/device_ops/cuda16/common.h>

namespace {

__global__ void reverse_fw_dev(
    const half *px, std::uint32_t skip, std::uint32_t n, std::uint32_t r, half *py) {
  const unsigned i = IDX;
  const unsigned j = IDY;
  const unsigned offset = i * n - i % skip * (n - 1);
  if (i < r && j < n) py[offset + j * skip] = px[offset + (n - j - 1) * skip];
}

__global__ void reverse_bw_dev(
    const half *pgy, std::uint32_t skip, std::uint32_t n, std::uint32_t r, half *pgx) {
  const unsigned i = IDX;
  const unsigned j = IDY;
  const unsigned offset = i * n - i % skip * (n - 1);
  if (i < r && j < n) INPLACE_ADD(pgx + offset + j * skip, ::__half2float(pgy[offset + (n - j - 1) * skip]));
}

}  // namespace

namespace primitiv {
namespace devices {

void CUDA16::reverse_fw_impl(
    const Tensor &x, std::uint32_t dim, Tensor &y) {
  const Shape &s = x.shape();
  const std::uint32_t n = s[dim];
  const std::uint32_t skip = s.lower_volume(dim);
  const std::uint32_t r = s.size() / n;
  const std::uint32_t g1 = GRID_SIZE(n, dim2_x_);
  const std::uint32_t g2 = GRID_SIZE(r, dim2_y_);
  CUDA_CALL(::cudaSetDevice(dev_id_));
  ::reverse_fw_dev<<<dim3(g1, g2, 1), dim3(dim2_x_, dim2_y_, 1)>>>(
      CDATA(half, x), skip, n, r, MDATA(half, y));
}

void CUDA16::reverse_bw_impl(
    const Tensor &gy, std::uint32_t dim, Tensor &gx) {
  const Shape &s = gy.shape();
  const std::uint32_t n = s[dim];
  const std::uint32_t skip = s.lower_volume(dim);
  const std::uint32_t r = s.size() / n;
  const std::uint32_t g1 = GRID_SIZE(n, dim2_x_);
  const std::uint32_t g2 = GRID_SIZE(r, dim2_y_);
  CUDA_CALL(::cudaSetDevice(dev_id_));
  ::reverse_bw_dev<<<dim3(g1, g2, 1), dim3(dim2_x_, dim2_y_, 1)>>>(
      CDATA(half, gy), skip, n, r, MDATA(half, gx));
}

}  // namespace devices
}  // namespace primitiv
