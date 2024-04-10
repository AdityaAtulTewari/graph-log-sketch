// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2023. University of Texas at Austin. All rights reserved.

#pragma once

#include <vector>
#include <algorithm>
#include <iterator>

#include "scea/graph/mutable_graph_interface.hpp"

namespace scea::graph {

class AdjGraph : public MutableGraph {
private:
  std::vector<std::vector<uint64_t>> edges;

public:
  explicit AdjGraph(uint64_t num_vertices) : edges(num_vertices) {}

  virtual ~AdjGraph() {}

  uint64_t size() noexcept override { return edges.size(); }

  uint64_t get_out_degree(uint64_t src) override { return edges[src].size(); }

  void add_edges(uint64_t src, const std::vector<uint64_t> dsts) override {
    std::copy(dsts.begin(), dsts.end(), std::back_inserter(edges[src]));
  }

  void post_ingest() override {}

  void for_each_edge(uint64_t src,
                     std::function<void(uint64_t const&)> callback) override {
    for (auto const& dst : edges[src])
      callback(dst);
  }

  void sort_edges(uint64_t src) {
    std::sort(edges[src].begin(), edges[src].end());
  }

  bool find_edge_sorted(uint64_t src, uint64_t dst) {
    return std::binary_search(edges[src].begin(), edges[src].end(), dst);
  }
};

} // namespace scea::graph
