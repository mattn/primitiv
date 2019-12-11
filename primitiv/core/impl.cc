#include <primitiv/config.h>

#define _PRIMITIV_DEFAULT_SETTABLE_INSTANTIATE
#include <primitiv/core/device.h>
#include <primitiv/core/graph.h>

// This function is not called from anywhere. Just for instantiate default_obj_
// of Device and Graph.
void _instantiate_() {
  primitiv::Device::get_reference_or_default(nullptr);
  primitiv::Graph::get_reference_or_default(nullptr);
}
