#include <config.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <primitiv/cpu_device.h>

using std::cerr;
using std::endl;

namespace primitiv {

CPUDevice::CPUDevice() : blocks_(), rng_(std::random_device()()) {}
CPUDevice::CPUDevice(const unsigned rng_seed) : blocks_(), rng_(rng_seed) {}

CPUDevice::~CPUDevice() {
  // check memory leak
  if (!blocks_.empty()) {
    cerr << "FATAL ERROR: Detected memory leak on CPUDevice!" << endl;
    cerr << "Leaked blocks (handle: size):" << endl;
    for (const auto &kv : blocks_) {
      cerr << "  " << kv.first << ": " << kv.second << endl;
    }
    std::abort();
  }
}

Tensor CPUDevice::new_tensor(const Shape &shape) {
  const unsigned mem_size = sizeof(float) * shape.size();
  void *data = std::malloc(mem_size);
  if (!data) {
    std::stringstream ss;
    ss << "Memory allocation failed. Requested size: " << mem_size;
    throw std::runtime_error(ss.str());
  }
  blocks_.insert(std::make_pair(data, mem_size));
  return Tensor(shape, this, data);
}

Tensor CPUDevice::new_tensor(const Shape &shape, const float k) {
  Tensor ret = new_tensor(shape);
  reset_tensor(ret, k);
  return ret;
}

Tensor CPUDevice::new_tensor(
    const Shape &shape, const std::vector<float> &values) {
  Tensor ret = new_tensor(shape);
  reset_tensor(ret, values);
  return ret;
}

void CPUDevice::delete_tensor(Tensor &x) {
  void *data = x.data();
  auto it = blocks_.find(data);
  if (it == blocks_.end()) {
    std::stringstream ss;
    ss << "Attempted to dispose unknown memory block: " << data;
    throw std::runtime_error(ss.str());
  }
  blocks_.erase(it);
  std::free(data);
}

#define CHECK_DEVICE(x) \
  if ((x).device() != this) { \
    std::stringstream ss; \
    ss << "Device mismatched. (" #x ").device(): " << (x).device() \
       << "!= this:" << this; \
    throw std::runtime_error(ss.str()); \
  }

#define DATA(x) static_cast<float *>((x).data())
#define CDATA(x) static_cast<const float *>((x).data())

#define REPEAT_OP(i, n, op) \
  for (unsigned (i) = 0; (i) < (n); ++(i)) { \
    (op); \
  }

std::vector<float> CPUDevice::tensor_to_vector(const Tensor &x) {
  CHECK_DEVICE(x);
  const unsigned num_elements = x.shape().size();
  std::vector<float> ret(num_elements);
  std::memcpy(&ret[0], x.data(), sizeof(float) * num_elements);
  return ret;
}

void CPUDevice::reset_tensor(Tensor &x, const float k) {
  CHECK_DEVICE(x);
  float *dest = DATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = k);
}

void CPUDevice::reset_tensor(Tensor &x, const std::vector<float> &values) {
  CHECK_DEVICE(x);
  const unsigned num_elements = x.shape().size();
  if (values.size() != num_elements) {
    std::stringstream ss;
    ss << "Data sizes mismatched. required: " << num_elements
       << " (shape: " << x.shape().to_string() << ") != actual: "
       << values.size();
    throw std::runtime_error(ss.str());
  }
  std::memcpy(x.data(), &values[0], sizeof(float) * num_elements);
}

Tensor CPUDevice::random_bernoulli(const Shape &shape, const float p) {
  std::bernoulli_distribution dist(p);
  Tensor ret = new_tensor(shape);
  float *dest = DATA(ret);
  const unsigned size = shape.size();
  for (unsigned i = 0; i < size; ++i) {
    dest[i] = dist(rng_);
  }
  return ret;
}

Tensor CPUDevice::random_uniform(
    const Shape &shape, const float lower, const float upper) {
  std::uniform_real_distribution<float> dist(lower, upper);
  Tensor ret = new_tensor(shape);
  float *dest = DATA(ret);
  const unsigned size = shape.size();
  for (unsigned i = 0; i < size; ++i) {
    const float x = dist(rng_);
    dest[i] = x == lower ? upper : x;
  }
  return ret;
}

Tensor CPUDevice::random_normal(
    const Shape &shape, const float mean, const float sd) {
  std::normal_distribution<float> dist(mean, sd);
  Tensor ret = new_tensor(shape);
  float *dest = DATA(ret);
  const unsigned size = shape.size();
  for (unsigned i = 0; i < size; ++i) {
    dest[i] = dist(rng_);
  }
  return ret;
}

Tensor CPUDevice::duplicate(const Tensor &x) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  std::memcpy(ret.data(), x.data(), sizeof(float) * x.shape().size());
  return ret;
}

Tensor CPUDevice::negate(const Tensor &x) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = -src[i]);
  return ret;
}

Tensor CPUDevice::add(const Tensor &x, const float k) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = src[i] + k);
  return ret;
}

Tensor CPUDevice::add(const Tensor &a, const Tensor &b) {
  CHECK_DEVICE(a);
  CHECK_DEVICE(b);
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const float *src_a = CDATA(a);
  const float *src_b = CDATA(b);

  if (sa.dims() == sb.dims()) {
    if (sa.batch_size() == sb.batch_size()) {
      // ret = a + b
      Tensor ret = new_tensor(sa);
      float *dest = DATA(ret);
      const unsigned size = sa.size();
      REPEAT_OP(i, size, dest[i] = src_a[i] + src_b[i]);
      return ret;
    } else if (sa.batch_size() == 1) {
      // ret = batch_broadcast(a) + b
      Tensor ret = new_tensor(sb);
      float *dest = DATA(ret);
      const unsigned ms = sa.size();
      const unsigned bs = sb.batch_size();
      for (unsigned k = 0; k < bs; ++k, dest += ms, src_b += ms) {
        REPEAT_OP(i, ms, dest[i] = src_a[i] + src_b[i]);
      }
      return ret;
    } else if (sb.batch_size() == 1) {
      // ret = a + batch_broadcast(b)
      Tensor ret = new_tensor(sa);
      float *dest = DATA(ret);
      const unsigned ms = sb.size();
      const unsigned bs = sa.batch_size();
      for (unsigned k = 0; k < bs; ++k, dest += ms, src_a += ms) {
        REPEAT_OP(i, ms, dest[i] = src_a[i] + src_b[i]);
      }
      return ret;
    }
  }

  // error
  std::stringstream ss;
  ss << "Attempted to add tensors with shapes "
     << a.shape().to_string() << " and " << b.shape().to_string() << '.';
  throw std::runtime_error(ss.str());
}

Tensor CPUDevice::subtract(const Tensor &x, const float k) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = src[i] - k);
  return ret;
}

Tensor CPUDevice::subtract(const float k, const Tensor &x) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = k - src[i]);
  return ret;
}

Tensor CPUDevice::subtract(const Tensor &a, const Tensor &b) {
  CHECK_DEVICE(a);
  CHECK_DEVICE(b);
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const float *src_a = CDATA(a);
  const float *src_b = CDATA(b);

  if (sa.dims() == sb.dims()) {
    if (sa.batch_size() == sb.batch_size()) {
      // ret = a - b
      Tensor ret = new_tensor(sa);
      float *dest = DATA(ret);
      const unsigned size = sa.size();
      REPEAT_OP(i, size, dest[i] = src_a[i] - src_b[i]);
      return ret;
    } else if (sa.batch_size() == 1) {
      // ret = batch_broadcast(a) - b
      Tensor ret = new_tensor(sb);
      float *dest = DATA(ret);
      const unsigned ms = sa.size();
      const unsigned bs = sb.batch_size();
      for (unsigned k = 0; k < bs; ++k, dest += ms, src_b += ms) {
        REPEAT_OP(i, ms, dest[i] = src_a[i] - src_b[i]);
      }
      return ret;
    } else if (sb.batch_size() == 1) {
      // ret = a - batch_broadcast(b)
      Tensor ret = new_tensor(sa);
      float *dest = DATA(ret);
      const unsigned ms = sb.size();
      const unsigned bs = sa.batch_size();
      for (unsigned k = 0; k < bs; ++k, dest += ms, src_a += ms) {
        REPEAT_OP(i, ms, dest[i] = src_a[i] - src_b[i]);
      }
      return ret;
    }
  }

  // error
  std::stringstream ss;
  ss << "Attempted to subtract tensors with shapes "
     << a.shape().to_string() << " and " << b.shape().to_string() << '.';
  throw std::runtime_error(ss.str());
}

Tensor CPUDevice::multiply(const Tensor &x, const float k) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = src[i] * k);
  return ret;
}

Tensor CPUDevice::multiply(const Tensor &a, const Tensor &b) {
  CHECK_DEVICE(a);
  CHECK_DEVICE(b);
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const float *src_a = CDATA(a);
  const float *src_b = CDATA(b);

  if (sa.dims() == sb.dims()) {
    if (sa.batch_size() == sb.batch_size()) {
      // ret = a * b
      Tensor ret = new_tensor(sa);
      float *dest = DATA(ret);
      const unsigned size = sa.size();
      REPEAT_OP(i, size, dest[i] = src_a[i] * src_b[i]);
      return ret;
    } else if (sa.batch_size() == 1) {
      // ret = batch_broadcast(a) * b
      Tensor ret = new_tensor(sb);
      float *dest = DATA(ret);
      const unsigned ms = sa.size();
      const unsigned bs = sb.batch_size();
      for (unsigned k = 0; k < bs; ++k, dest += ms, src_b += ms) {
        REPEAT_OP(i, ms, dest[i] = src_a[i] * src_b[i]);
      }
      return ret;
    } else if (sb.batch_size() == 1) {
      // ret = a * batch_broadcast(b)
      Tensor ret = new_tensor(sa);
      float *dest = DATA(ret);
      const unsigned ms = sb.size();
      const unsigned bs = sa.batch_size();
      for (unsigned k = 0; k < bs; ++k, dest += ms, src_a += ms) {
        REPEAT_OP(i, ms, dest[i] = src_a[i] * src_b[i]);
      }
      return ret;
    }
  }

  // error
  std::stringstream ss;
  ss << "Attempted to multiply tensors with shapes "
     << a.shape().to_string() << " and " << b.shape().to_string() << '.';
  throw std::runtime_error(ss.str());
}

Tensor CPUDevice::divide(const Tensor &x, const float k) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = src[i] / k);
  return ret;
}

Tensor CPUDevice::divide(const float k, const Tensor &x) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = k / src[i]);
  return ret;
}

Tensor CPUDevice::divide(const Tensor &a, const Tensor &b) {
  CHECK_DEVICE(a);
  CHECK_DEVICE(b);
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const float *src_a = CDATA(a);
  const float *src_b = CDATA(b);

  if (sa.dims() == sb.dims()) {
    if (sa.batch_size() == sb.batch_size()) {
      // ret = a / b
      Tensor ret = new_tensor(sa);
      float *dest = DATA(ret);
      const unsigned size = sa.size();
      REPEAT_OP(i, size, dest[i] = src_a[i] / src_b[i]);
      return ret;
    } else if (sa.batch_size() == 1) {
      // ret = batch_broadcast(a) / b
      Tensor ret = new_tensor(sb);
      float *dest = DATA(ret);
      const unsigned ms = sa.size();
      const unsigned bs = sb.batch_size();
      for (unsigned k = 0; k < bs; ++k, dest += ms, src_b += ms) {
        REPEAT_OP(i, ms, dest[i] = src_a[i] / src_b[i]);
      }
      return ret;
    } else if (sb.batch_size() == 1) {
      // ret = a / batch_broadcast(b)
      Tensor ret = new_tensor(sa);
      float *dest = DATA(ret);
      const unsigned ms = sb.size();
      const unsigned bs = sa.batch_size();
      for (unsigned k = 0; k < bs; ++k, dest += ms, src_a += ms) {
        REPEAT_OP(i, ms, dest[i] = src_a[i] / src_b[i]);
      }
      return ret;
    }
  }

  // error
  std::stringstream ss;
  ss << "Attempted to divide tensors with shapes "
     << a.shape().to_string() << " and " << b.shape().to_string() << '.';
  throw std::runtime_error(ss.str());
}

Tensor CPUDevice::transpose(const Tensor &x) {
  CHECK_DEVICE(x);
  const Shape &s = x.shape();
  if (s.dims().size() > 2) {
    std::stringstream ss;
    ss << "Attempted to transpose a tensor with shape " << s.to_string() << '.';
    throw std::runtime_error(ss.str());
  }

  const unsigned d1 = s.dim(0);
  const unsigned d2 = s.dim(1);
  const unsigned ms = d1 * d2;
  const unsigned bs = s.batch_size();
  Tensor ret = new_tensor(Shape({d2, d1}, bs));
  float *dest = DATA(ret);
  const float *src = CDATA(x);

  for (unsigned k = 0; k < bs; ++k) {
    float *pd = dest;
    const float *ps = src;
    for (unsigned j = 0; j < d2; ++j) {
      float *ppd = pd;
      for (unsigned i = 0; i < d1; ++i) {
        *ppd = ps[i];
        ppd += d2;
      }
      ++pd;
      ps += d1;
    }
    dest += ms;
    src += ms;
  }

  return ret;
}

Tensor CPUDevice::dot(const Tensor &a, const Tensor &b) {
  CHECK_DEVICE(a);
  CHECK_DEVICE(b);
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  const float *src_a = CDATA(a);
  const float *src_b = CDATA(b);
  const unsigned d1 = sa.dim(0);
  const unsigned d2 = sa.dim(1);
  const unsigned d3 = sb.dim(1);
  const unsigned dest_shift = d1 * d3;
  const unsigned src_a_shift = d1 * d2;
  const unsigned src_b_shift = d2 * d3;

  if (sa.dims().size() <= 2 && sb.dims().size() <= 2 && d2 == sb.dim(0)) {
    if (sa.batch_size() == sb.batch_size()) {
      // ret = a . b
      const unsigned bs = sa.batch_size();
      Tensor ret = new_tensor(Shape({d1, d3}, bs));
      float *dest = DATA(ret);
      for (unsigned b = 0; b < bs; ++b) {
        for (unsigned i = 0; i < d1; ++i) {
          for (unsigned k = 0; k < d3; ++k) {
            float tmp = 0;
            for (unsigned j = 0; j < d2; ++j) {
              tmp += src_a[i + j * d1] * src_b[j + k * d2];
            }
            dest[i + k * d1] = tmp;
          }
        }
        dest += dest_shift;
        src_a += src_a_shift;
        src_b += src_b_shift;
      }
      return ret;
    } else if (sa.batch_size() == 1) {
      // ret = batch_broadcast(a) . b
      const unsigned bs = sb.batch_size();
      Tensor ret = new_tensor(Shape({d1, d3}, bs));
      float *dest = DATA(ret);
      for (unsigned b = 0; b < bs; ++b) {
        for (unsigned i = 0; i < d1; ++i) {
          for (unsigned k = 0; k < d3; ++k) {
            float tmp = 0;
            for (unsigned j = 0; j < d2; ++j) {
              tmp += src_a[i + j * d1] * src_b[j + k * d2];
            }
            dest[i + k * d1] = tmp;
          }
        }
        dest += dest_shift;
        src_b += src_b_shift;
      }
      return ret;
    } else if (sb.batch_size() == 1) {
      // ret = a . batch_broadcast(b)
      const unsigned bs = sa.batch_size();
      Tensor ret = new_tensor(Shape({d1, d3}, bs));
      float *dest = DATA(ret);
      for (unsigned b = 0; b < bs; ++b) {
        for (unsigned i = 0; i < d1; ++i) {
          for (unsigned k = 0; k < d3; ++k) {
            float tmp = 0;
            for (unsigned j = 0; j < d2; ++j) {
              tmp += src_a[i + j * d1] * src_b[j + k * d2];
            }
            dest[i + k * d1] = tmp;
          }
        }
        dest += dest_shift;
        src_a += src_a_shift;
      }
      return ret;
    }
  }

  // error
  std::stringstream ss;
  ss << "Attempted to calculate the dot product of tensors with shapes "
     << a.shape().to_string() << " and " << b.shape().to_string() << '.';
  throw std::runtime_error(ss.str());
}

Tensor CPUDevice::exp(const Tensor &x) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = std::exp(src[i]));
  return ret;
}

Tensor CPUDevice::tanh(const Tensor &x) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = std::tanh(src[i]));
  return ret;
}

Tensor CPUDevice::sigmoid(const Tensor &x) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = .5 + .5 * std::tanh(.5 * src[i]));
  return ret;
}

Tensor CPUDevice::step(const Tensor &x) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = static_cast<float>(src[i] > 0));
  return ret;
}

Tensor CPUDevice::relu(const Tensor &x) {
  CHECK_DEVICE(x);

  Tensor ret = new_tensor(x.shape());
  float *dest = DATA(ret);
  const float *src = CDATA(x);
  const unsigned size = x.shape().size();
  REPEAT_OP(i, size, dest[i] = std::max(src[i], .0f));
  return ret;
}

void CPUDevice::add_gradient(Tensor &a, const Tensor &b) {
  CHECK_DEVICE(a);
  CHECK_DEVICE(b);
  const Shape &sa = a.shape();
  const Shape &sb = b.shape();
  float *dest = DATA(a);
  const float *src = CDATA(b);

  if (sa.dims() == sb.dims()) {
    if (sa.batch_size() == sb.batch_size()) {
      // a += b
      const unsigned size = sa.size();
      REPEAT_OP(i, size, dest[i] += src[i]);
      return;
    } else if (sa.batch_size() == 1) {
      // a += batch_sum(b)
      const unsigned ms = sa.size();
      const unsigned bs = sb.batch_size();
      for (unsigned k = 0; k < bs; ++k, src += ms) {
        REPEAT_OP(i, ms, dest[i] += src[i]);
      }
      return;
    } else if (sb.batch_size() == 1) {
      // a += batch_broadcast(b)
      const unsigned ms = sb.size();
      const unsigned bs = sa.batch_size();
      for (unsigned k = 0; k < bs; ++k, dest += ms) {
        REPEAT_OP(i, ms, dest[i] += src[i]);
      }
      return;
    }
  }

  // error
  std::stringstream ss;
  ss << "Attempted to add gradients with shape " << sb.to_string()
     << " to shape " << sa.to_string() << '.';
  throw std::runtime_error(ss.str());
}

}  // namespace primitiv
