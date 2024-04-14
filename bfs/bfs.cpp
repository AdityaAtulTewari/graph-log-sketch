#include <iostream>
#include "importer.cpp"
#include "galois/graphs/DistributedLocalGraph.h"
#include "galois/graphs/GluonSubstrate.h"
#include "galois/wmd/WMDPartitioner.h"
#include "galois/graphs/GenericPartitioners.h"
#include "galois/DTerminationDetector.h"
#include "galois/DistGalois.h"
#include "galois/DReducible.h"
#include "galois/gstl.h"
#include "galois/DistGalois.h"
#include "galois/runtime/SyncStructures.h"
#include "galois/DReducible.h"
#include "galois/DTerminationDetector.h"
#include "galois/gstl.h"
#include "galois/runtime/Tracer.h"

#include <iostream>
#include <limits>

const uint32_t infinity = std::numeric_limits<uint32_t>::max() / 4;

struct NodeData {
  uint32_t dist_current;
};

uint64_t src_node = 0;
uint64_t maxIterations = 1000;

typedef galois::graphs::DistLocalGraph<NodeData, void> Graph;
typedef typename Graph::GraphNode GNode;
std::unique_ptr<galois::graphs::GluonSubstrate<Graph>> syncSubstrate;

galois::DynamicBitSet bitset_dist_current;
//#include "bfs_pull_sync.hh"

/******************************************************************************/
/* Algorithm structures */
/******************************************************************************/

struct InitializeGraph {
  const uint32_t& local_infinity;
  uint64_t& local_src_node;
  Graph* graph;

  InitializeGraph(uint64_t& _src_node, const uint32_t& _infinity,
                  Graph* _graph)
      : local_infinity(_infinity), local_src_node(_src_node), graph(_graph) {}

  void static go(Graph& _graph) {
    const auto& allNodes = _graph.allNodesRange();
    galois::do_all(
        galois::iterate(allNodes),
        InitializeGraph(src_node, infinity, &_graph), galois::no_stats(),
        galois::loopname(
            syncSubstrate->get_run_identifier("InitializeGraph").c_str()));
  }

  void operator()(GNode src) const {
    NodeData& sdata = graph->getData(src);
    sdata.dist_current =
        (graph->getGID(src) == local_src_node) ? 0 : local_infinity;
  }
};

template <bool async>
struct BFS {
  Graph* graph;
  using DGTerminatorDetector =
      typename std::conditional<async, galois::DGTerminator<unsigned int>,
                                galois::DGAccumulator<unsigned int>>::type;

  DGTerminatorDetector& active_vertices;

  BFS(Graph* _graph, DGTerminatorDetector& _dga)
      : graph(_graph), active_vertices(_dga) {}

  void static go(Graph& _graph) {
    unsigned _num_iterations = 0;
    DGTerminatorDetector dga;

    const auto& nodesWithEdges = _graph.allNodesWithEdgesRange();
    do {
      syncSubstrate->set_num_round(_num_iterations);
      dga.reset();
      galois::do_all(
          galois::iterate(nodesWithEdges), BFS(&_graph, dga),
          galois::no_stats(), galois::steal(),
          galois::loopname(syncSubstrate->get_run_identifier("BFS").c_str()));
      syncSubstrate->sync<writeSource, readDestination, Reduce_min_dist_current,
                          Bitset_dist_current, async>("BFS");

      galois::runtime::reportStat_Tsum(
          "BFS", syncSubstrate->get_run_identifier("NumWorkItems"),
          (unsigned long)dga.read_local());
      ++_num_iterations;
    } while ((async || (_num_iterations < maxIterations)) &&
             dga.reduce(syncSubstrate->get_run_identifier()));

    if (galois::runtime::getSystemNetworkInterface().ID == 0) {
      galois::runtime::reportStat_Single(
          "BFS",
          "NumIterations_" + std::to_string(syncSubstrate->get_run_num()),
          (unsigned long)_num_iterations);
    }
  }

  void operator()(GNode src) const {
    NodeData& snode = graph->getData(src);

    for (auto jj : graph->edges(src)) {
      GNode dst         = graph->getEdgeDst(jj);
      auto& dnode       = graph->getData(dst);
      uint32_t new_dist = dnode.dist_current + 1;
      uint32_t old_dist = galois::min(snode.dist_current, new_dist);
      if (old_dist > new_dist) {
        bitset_dist_current.set(src);
        active_vertices += 1;
      }
    }
  }
};

/******************************************************************************/
/* Sanity check operators */
/******************************************************************************/

/* Prints total number of nodes visited + max distance */
struct BFSSanityCheck {
  const uint32_t& local_infinity;
  Graph* graph;

  galois::DGAccumulator<uint64_t>& DGAccumulator_sum;
  galois::DGReduceMax<uint32_t>& DGMax;

  BFSSanityCheck(const uint32_t& _infinity, Graph* _graph,
                 galois::DGAccumulator<uint64_t>& dgas,
                 galois::DGReduceMax<uint32_t>& dgm)
      : local_infinity(_infinity), graph(_graph), DGAccumulator_sum(dgas),
        DGMax(dgm) {}

  void static go(Graph& _graph, galois::DGAccumulator<uint64_t>& dgas,
                 galois::DGReduceMax<uint32_t>& dgm) {
    dgas.reset();
    dgm.reset();

    galois::do_all(galois::iterate(_graph.masterNodesRange().begin(),
                                     _graph.masterNodesRange().end()),
                     BFSSanityCheck(infinity, &_graph, dgas, dgm),
                     galois::no_stats(), galois::loopname("BFSSanityCheck"));

    uint64_t num_visited  = dgas.reduce();
    uint32_t max_distance = dgm.reduce();

    // Only host 0 will print the info
    if (galois::runtime::getSystemNetworkInterface().ID == 0) {
      galois::gPrint("Number of nodes visited from source ", src_node, " is ",
                     num_visited, "\n");
      galois::gPrint("Max distance from source ", src_node, " is ",
                     max_distance, "\n");
    }
  }

  void operator()(GNode src) const {
    NodeData& src_data = graph->getData(src);

    if (src_data.dist_current < local_infinity) {
      DGAccumulator_sum += 1;
      DGMax.update(src_data.dist_current);
    }
  }
};

/******************************************************************************/
/* Make results */
/******************************************************************************/

std::vector<uint32_t> makeResultsCPU(std::unique_ptr<Graph>& hg) {
  std::vector<uint32_t> values;

  values.reserve(hg->numMasters());
  for (auto node : hg->masterNodesRange()) {
    values.push_back(hg->getData(node).dist_current);
  }

  return values;
}

int main(int argc, char* argv[]) {

  std::string filename = argv[1];
  if(argc > 2) {
    src_node = std::stoul(argv[2]);
  }
  std::unique_ptr<Graph> hg;

  hg = distLocalGraphInitialization<galois::graphs::ELVertex, galois::graphs::ELEdge, NodeData, void, OECPolicy>(filename);

  syncSubstrate = gluonInitialization<NodeData, void>(hg);

  return 0;
}