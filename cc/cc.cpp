// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2023. University of Texas at Austin. All rights reserved.

#include <iostream>
#include <limits>

#include "../include/importer.hpp"

#include "galois/graphs/DistributedLocalGraph.h"
#include "galois/graphs/GluonSubstrate.h"
#include "galois/wmd/WMDPartitioner.h"
#include "galois/graphs/GenericPartitioners.h"
#include "galois/DistGalois.h"
#include "galois/DTerminationDetector.h"
#include "galois/runtime/SyncStructures.h"
#include "galois/DReducible.h"
#include "galois/gstl.h"
#include "galois/runtime/Tracer.h"
#include "galois/runtime/GraphUpdateManager.h"

struct NodeData {
  std::atomic<uint32_t> comp_current;
  uint32_t comp_old;
  NodeData() : comp_current(std::numeric_limits<uint32_t>::max()), comp_old(0) {}
  NodeData(uint32_t x) : comp_current(x), comp_old(0) {}
  NodeData(const NodeData& o) : comp_current(o.comp_current.load()), comp_old(o.comp_old) {}
};

constexpr static const char* const REGION_NAME = "ConnectedComp";

galois::DynamicBitSet bitset_comp_current;

typedef galois::graphs::DistLocalGraph<NodeData, void> Graph;
typedef galois::graphs::WMDGraph<galois::graphs::ELVertex, galois::graphs::ELEdge, NodeData, void, OECPolicy> ELGraph;
typedef typename Graph::GraphNode GNode;

std::unique_ptr<galois::graphs::GluonSubstrate<Graph>> syncSubstrate;

uint64_t maxIterations = 1000;

#include "cc_push_sync.hh"

/******************************************************************************/
/* Algorithm structures */
/******************************************************************************/

struct InitializeGraph {
  Graph* graph;

  InitializeGraph(Graph* _graph) : graph(_graph) {}

  void static go(Graph& _graph) {
    const auto& allNodes = _graph.allNodesRange();

    galois::do_all(
        galois::iterate(allNodes.begin(), allNodes.end()),
        InitializeGraph{&_graph}, galois::no_stats(),
        galois::loopname(
            syncSubstrate->get_run_identifier("InitializeGraph").c_str()));
  }

  void operator()(GNode src) const {
    NodeData& sdata    = graph->getData(src);
    sdata.comp_current = graph->getGID(src);
    sdata.comp_old     = graph->getGID(src);
  }
};

template <bool async>
struct FirstItr_ConnectedComp {
  Graph* graph;
  FirstItr_ConnectedComp(Graph* _graph) : graph(_graph) {}

  void static go(Graph& _graph) {
    const auto& nodesWithEdges = _graph.allNodesWithEdgesRange();
    syncSubstrate->set_num_round(0);
    galois::do_all(
        galois::iterate(nodesWithEdges), FirstItr_ConnectedComp{&_graph},
        galois::steal(), galois::no_stats(),
        galois::loopname(
            syncSubstrate->get_run_identifier("ConnectedComp").c_str()));

    syncSubstrate->sync<writeDestination, readSource, Reduce_min_comp_current,
                        Bitset_comp_current, async>("ConnectedComp");

    galois::runtime::reportStat_Tsum(
        REGION_NAME, "NumWorkItems_" + (syncSubstrate->get_run_identifier()),
        _graph.allNodesRange().end() - _graph.allNodesRange().begin());
  }

  void operator()(GNode src) const {
    NodeData& snode = graph->getData(src);
    snode.comp_old  = snode.comp_current;

    for (auto jj : graph->edges(src)) {
      GNode dst         = graph->getEdgeDst(jj);
      auto& dnode       = graph->getData(dst);
      uint32_t new_dist = snode.comp_current;
      uint32_t old_dist = galois::atomicMin(dnode.comp_current, new_dist);
      if (old_dist > new_dist)
        bitset_comp_current.set(dst);
    }
  }
};

template <bool async>
struct ConnectedComp {
  Graph* graph;
  using DGTerminatorDetector =
      typename std::conditional<async, galois::DGTerminator<unsigned int>,
                                galois::DGAccumulator<unsigned int>>::type;

  DGTerminatorDetector& active_vertices;

  ConnectedComp(Graph* _graph, DGTerminatorDetector& _dga)
      : graph(_graph), active_vertices(_dga) {}

  void static go(Graph& _graph) {
    using namespace galois::worklists;

    FirstItr_ConnectedComp<async>::go(_graph);

    unsigned _num_iterations = 1;
    DGTerminatorDetector dga;

    const auto& nodesWithEdges = _graph.allNodesWithEdgesRange();

    do {
      syncSubstrate->set_num_round(_num_iterations);
      dga.reset();
      galois::do_all(
          galois::iterate(nodesWithEdges), ConnectedComp(&_graph, dga),
          galois::no_stats(), galois::steal(),
          galois::loopname(
              syncSubstrate->get_run_identifier("ConnectedComp").c_str()));

      syncSubstrate->sync<writeDestination, readSource, Reduce_min_comp_current,
                          Bitset_comp_current, async>("ConnectedComp");

      galois::runtime::reportStat_Tsum(
          REGION_NAME, "NumWorkItems_" + (syncSubstrate->get_run_identifier()),
          (unsigned long)dga.read_local());
      ++_num_iterations;
    } while ((async || (_num_iterations < maxIterations)) &&
             dga.reduce(syncSubstrate->get_run_identifier()));

    galois::runtime::reportStat_Tmax(
        REGION_NAME,
        "NumIterations_" + std::to_string(syncSubstrate->get_run_num()),
        (unsigned long)_num_iterations);
  }

  void operator()(GNode src) const {
    NodeData& snode = graph->getData(src);

    if (snode.comp_old > snode.comp_current) {
      snode.comp_old = snode.comp_current;

      for (auto jj : graph->edges(src)) {
        active_vertices += 1;

        GNode dst         = graph->getEdgeDst(jj);
        auto& dnode       = graph->getData(dst);
        uint32_t new_dist = snode.comp_current;
        uint32_t old_dist = galois::atomicMin(dnode.comp_current, new_dist);
        if (old_dist > new_dist)
          bitset_comp_current.set(dst);
      }
    }
  }
};

/******************************************************************************/
/* Sanity check operators */
/******************************************************************************/

/* Get/print the number of components */
struct ConnectedCompSanityCheck {
  Graph* graph;

  galois::DGAccumulator<uint64_t>& active_vertices;

  ConnectedCompSanityCheck(Graph* _graph, galois::DGAccumulator<uint64_t>& _dga)
      : graph(_graph), active_vertices(_dga) {}

  void static go(Graph& _graph, galois::DGAccumulator<uint64_t>& dga) {
    dga.reset();

    galois::do_all(galois::iterate(_graph.masterNodesRange().begin(),
                                   _graph.masterNodesRange().end()),
                   ConnectedCompSanityCheck(&_graph, dga), galois::no_stats(),
                   galois::loopname("ConnectedCompSanityCheck"));

    uint64_t num_components = dga.reduce();

    // Only node 0 will print the number visited
    if (galois::runtime::getSystemNetworkInterface().ID == 0) {
      galois::gPrint("Number of components is ", num_components, "\n");
    }
  }

  /* Check if a node's component is the same as its ID.
   * if yes, then increment an accumulator */
  void operator()(GNode src) const {
    NodeData& src_data = graph->getData(src);

    if (src_data.comp_current == graph->getGID(src)) {
      active_vertices += 1;
    }
  }
};

const char* elGetOne(const char* line, std::uint64_t& val) {
  bool found = false;
  val        = 0;
  char c;
  while ((c = *line++) != '\0' && isspace(c)) {
  }
  do {
    if (isdigit(c)) {
      found = true;
      val *= 10;
      val += (c - '0');
    } else if (c == '_') {
      continue;
    } else {
      break;
    }
  } while ((c = *line++) != '\0' && !isspace(c));
  if (!found)
    val = UINT64_MAX;
  return line;
}

void parser(const char* line, Graph &hg, std::vector<std::vector<uint64_t>> &delta_mirrors) {
  uint64_t src, dst;
  line = elGetOne(line, src);
  line = elGetOne(line, dst);
  std::cout << "src: " << src << " dst: " << dst << " isowned " << hg.isOwned(src) << " " << hg.isOwned(dst) << "\n";
  if((hg.isOwned(src)) && (!hg.isLocal(dst))) {
    uint32_t h = hg.getHostID(dst);
    delta_mirrors[h].push_back(dst);
  }
}

std::vector<std::vector<uint64_t>> genMirrorNodes(Graph &hg, std::string filename, int batch) {

  auto& net = galois::runtime::getSystemNetworkInterface();
  std::vector<std::vector<uint64_t>> delta_mirrors(net.Num);

  for(uint32_t i=0; i<net.Num; i++) {
    std::string dynamicFile = filename + "_batch" + std::to_string(batch) + "_host" + std::to_string(i) + ".el";
    std::ifstream file(dynamicFile);
    std::string line;
    while (std::getline(file, line)) {
      parser(line.c_str(), hg, delta_mirrors);
    }
  }
  return delta_mirrors;
}

void PrintMasterMirrorNodes (Graph &hg, uint64_t id) {
  std::cout << "Master nodes on host " << id <<std::endl;
  for (auto node : hg.masterNodesRange()) {
    std::cout << hg.getGID(node) << " ";
  }
  std::cout << std::endl;
  std::cout << "Mirror nodes on host " << id <<std::endl;
  auto mirrors = hg.getMirrorNodes();
  for (auto vec : mirrors) {
    for (auto node : vec) {
      std::cout << node << " ";
    }
  }
  std::cout << std::endl;
}

int main(int argc, char* argv[]) {

  if (argc < 5) {
    std::cerr << "Usage: " << argv[0]
              << " <filename> <numVertices> <numBatches> <maxEditsInBatch>\n";
    return 1;
  }

  std::string filename = argv[1];
  uint64_t numVertices = std::stoul(argv[2]);
  uint64_t num_batches = std::stoul(argv[3]);
  uint64_t max_edits_in_batch = std::stoul(argv[4]);

  galois::DistMemSys G;

  std::unique_ptr<Graph> hg;

  hg            = distLocalGraphInitialization<galois::graphs::ELVertex,
                                    galois::graphs::ELEdge, NodeData, void,
                                    OECPolicy>(filename, numVertices);
  syncSubstrate = gluonInitialization<NodeData, void>(hg);

  bitset_comp_current.resize(hg->size());

  galois::runtime::getHostBarrier().wait();

  ELGraph* wg = dynamic_cast<ELGraph*>(hg.get());

  auto& net = galois::runtime::getSystemNetworkInterface();

  for (int i=0; i<num_batches; i++) {
    PrintMasterMirrorNodes(*hg, net.ID);

    std::vector<std::string> edit_files;
    std::string dynFile = "edits";
    std::string dynamicFile = dynFile + "_batch" + std::to_string(i) + "_host" + std::to_string(net.ID) + ".el";
    edit_files.emplace_back(dynamicFile);
    //IMPORTANT: CAll genMirrorNodes before creating the graphUpdateManager!!!!!!!!
    std::vector<std::vector<uint64_t>> delta_mirrors = genMirrorNodes(*hg, dynFile, i);
    std::cout << "Starting Graph Update Manager" << std::endl;
    graphUpdateManager<galois::graphs::ELVertex,
                                      galois::graphs::ELEdge, NodeData, void, OECPolicy> GUM(std::make_unique<galois::graphs::ELParser<galois::graphs::ELVertex,
                                      galois::graphs::ELEdge>> (1, edit_files), 100, wg);
    GUM.setBatchSize(max_edits_in_batch);
    GUM.start();
    while (!GUM.stop()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(GUM.getPeriod()));
    }
    std::cout << "Finished Graph Update Manager" << std::endl;
    galois::runtime::getHostBarrier().wait();
    GUM.stop2();


    syncSubstrate->addDeltaMirrors(delta_mirrors);
    PrintMasterMirrorNodes(*hg, net.ID);
    galois::runtime::getHostBarrier().wait();

    InitializeGraph::go((*hg));
    galois::runtime::getHostBarrier().wait();

    galois::DGAccumulator<uint64_t> active_vertices64;

    ConnectedComp<false>::go(*hg);

    ConnectedCompSanityCheck::go(*hg, active_vertices64);

    if ((i + 1) != 1) {
      bitset_comp_current.reset();

      (*syncSubstrate).set_num_run(i + 1);
      InitializeGraph::go((*hg));
      galois::runtime::getHostBarrier().wait();
    }
  }

  return 0;
}