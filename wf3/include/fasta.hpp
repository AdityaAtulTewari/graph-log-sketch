//===------------------------------------------------------------*- C++ -*-===//
////
////                            The AGILE Workflows
////
////===----------------------------------------------------------------------===//
//// ** Pre-Copyright Notice
////
//// This computer software was prepared by Battelle Memorial Institute,
//// hereinafter the Contractor, under Contract No. DE-AC05-76RL01830 with the
//// Department of Energy (DOE). All rights in the computer software are
///reserved / by DOE on behalf of the United States Government and the
///Contractor as / provided in the Contract. You are authorized to use this
///computer software / for Governmental purposes but it is not to be released or
///distributed to the / public. NEITHER THE GOVERNMENT NOR THE CONTRACTOR MAKES
///ANY WARRANTY, EXPRESS / OR IMPLIED, OR ASSUMES ANY LIABILITY FOR THE USE OF
///THIS SOFTWARE. This / notice including this sentence must appear on any
///copies of this computer / software.
////
//// ** Disclaimer Notice
////
//// This material was prepared as an account of work sponsored by an agency of
//// the United States Government. Neither the United States Government nor the
//// United States Department of Energy, nor Battelle, nor any of their
///employees, / nor any jurisdiction or organization that has cooperated in the
///development / of these materials, makes any warranty, express or implied, or
///assumes any / legal liability or responsibility for the accuracy,
///completeness, or / usefulness or any information, apparatus, product,
///software, or process / disclosed, or represents that its use would not
///infringe privately owned / rights. Reference herein to any specific
///commercial product, process, or / service by trade name, trademark,
///manufacturer, or otherwise does not / necessarily constitute or imply its
///endorsement, recommendation, or favoring / by the United States Government or
///any agency thereof, or Battelle Memorial / Institute. The views and opinions
///of authors expressed herein do not / necessarily state or reflect those of
///the United States Government or any / agency thereof.
////
////                    PACIFIC NORTHWEST NATIONAL LABORATORY
////                                 operated by
////                                   BATTELLE
////                                   for the
////                      UNITED STATES DEPARTMENT OF ENERGY
////                       under Contract DE-AC05-76RL01830
////===----------------------------------------------------------------------===//

#ifndef WF3_FASTA_H_
#define WF3_FASTA_H_

#include "pakman.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace pakman {

std::unique_ptr<pakman::PakmanGraph> ingest(std::string filename,
                                            uint64_t mn_length,
                                            uint64_t coverage,
                                            uint64_t min_length_count);
std::unordered_map<uint64_t, uint32_t> read(std::string filename,
                                            uint64_t mn_length);
std::vector<uint64_t>
getBucketCounts(std::unordered_map<uint64_t, uint32_t> kmers,
                uint64_t min_length_count);
std::unique_ptr<pakman::PakmanGraph>
createPakmanNodes(std::unordered_map<uint64_t, uint32_t>&& kmers,
                  uint64_t mnLength, uint64_t coverage, uint64_t min_index);

} // namespace pakman

#endif
