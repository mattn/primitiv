#include <primitiv/config.h>

#include <primitiv/core/mixins/default_settable.h>
#include <primitiv/core/device.h>
#include <primitiv/core/graph.h>

namespace primitiv {

template<typename T>
T *mixins::DefaultSettable<T>::default_obj_ = nullptr;


// This function is not called from anywhere. Just for instantiate default_obj_
// of Device and Graph.
void _instantiate_() {
  Device::get_default();
  Graph::get_default();
}

}  // namespace primitiv
