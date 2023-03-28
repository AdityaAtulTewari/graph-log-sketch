#include <iostream>
#include <sstream>
#include <mpi.h>
#include "galois/Galois.h"
#include "galois/DistGalois.h"

struct RMAT_args_t
{
  uint64_t seed;
  uint64_t scale;
  double A;
  double B;
  double C;
  double D;
};

void RMAT(const RMAT_args_t & args, uint64_t ndx, galois::InsertBag<std::pair<uint64_t, uint64_t>>& edges)
{
  std::mt19937_64 gen64(args.seed + ndx);
  uint64_t max_rand = gen64.max();
  uint64_t src = 0, dst = 0;

  for (uint64_t i = 0; i < args.scale; i ++) {
    double rand = (double) gen64() / (double) max_rand;
    if      (rand <= args.A)                            { src = (src << 1) + 0; dst = (dst << 1) + 0; }
    else if (rand <= args.A + args.B)                   { src = (src << 1) + 0; dst = (dst << 1) + 1; }
    else if (rand <= args.A + args.B + args.C)          { src = (src << 1) + 1; dst = (dst << 1) + 0; }
    else if (rand <= args.A + args.B + args.C + args.D) { src = (src << 1) + 1; dst = (dst << 1) + 1; }
  }

  uint64_t num_v = 1 << args.scale;
  if (src != dst) {                          // do not include self edges
    if (src > dst) std::swap(src, dst);     // make src less than dst
    edges.emplace_back(src % num_v, dst % num_v);
  }
}

int main(int argc, char** argv)
{
  galois::DistMemSys G;

  auto& net = galois::runtime::getSystemNetworkInterface();
  auto myrank = net.ID;
  auto nprocs = net.Num;

  if (argc != 8)
  {
    if(myrank == 0) printf("Usage: <nthreads> <seed> <scale> <edge ratio> <A> <B> <C>\n");
    MPI_Finalize();
    return 1;
  }

  galois::setActiveThreads(std::stoll(argv[1]));
  uint64_t seed = std::stoll(argv[2]);
  uint64_t scale = std::stoll(argv[3]);
  uint64_t num_vertices = (uint64_t) 1 << scale;
  uint64_t num_edges = num_vertices * std::stoll(argv[4]);

  num_edges = (myrank < (num_edges % nprocs)) ? num_edges/nprocs + 1 : num_edges/nprocs;

  using namespace galois;

  double A = std::stod(argv[5]) / 100.0;
  double B = std::stod(argv[6]) / 100.0;
  double C = std::stod(argv[7]) / 100.0;
  double D = 1.0 - A - B - C;

  if ( (A <= 0.0) || (B <= 0.0) || (C <= 0.0) || (A + B + C >= 1.0) )
     {printf("sector probablitiies must be greater than 0.0 and sum to 1.0\n"); return 1;}

  RMAT_args_t rmat_args = {seed + myrank, scale, A, B, C, D};

  InsertBag<std::pair<uint64_t, uint64_t>> edges;

  //generate the random edges
  double t02;
  galois::runtime::getHostBarrier().wait();
  double t01 = MPI_Wtime();
  galois::runtime::getHostBarrier().wait();
  galois::do_all(galois::iterate((uint64_t)0, num_edges),
      [&](uint64_t i)
      {
        RMAT(rmat_args, i, edges);
      }, galois::steal());
  galois::runtime::getHostBarrier().wait();
  t02 = MPI_Wtime();

  if(myrank == 0) std::cerr << "Graph creation time:" << "\t" << t02 - t01 << std::endl;

  for(uint64_t i = 0; i < nprocs; i++)
  {
    galois::do_all(galois::iterate(edges.begin(),edges.end()),
        [&](std::pair<uint64_t, uint64_t>& p)
        {
          std::stringstream sout;
          sout << p.first << "\t" << p.second << std::endl;
          std::cout << sout.str();
        });
    galois::runtime::getHostBarrier().wait();
  }
  MPI_Finalize();
}