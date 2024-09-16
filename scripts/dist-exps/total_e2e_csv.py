# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2023. University of Texas at Austin. All rights reserved.

import subprocess
import re
import matplotlib.pyplot as plt
import numpy as np
import csv
import argparse
import os


def parse(log_path: str, algo: str):
   print(f"Export {log_path} on {algo} to csv..")
   command = f"cat {log_path}"

   process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
   stdout, stderr = process.communicate()

   output = stdout.decode()

   # Parse batch number and duration
   ingestion_pattern = re.compile(rf"Benchmark {algo} ingestion results for batch (\d+):.*?Duration: (\d+) nanoseconds", re.DOTALL)
   algorithm_pattern = re.compile(rf"Benchmark {algo} algorithm results for batch (\d+):.*?Duration: (\d+) nanoseconds", re.DOTALL)

   ingestion_durations = {}
   algorithm_durations = {}

   ingestion_matches = re.findall(ingestion_pattern, output)
   algorithm_matches = re.findall(algorithm_pattern, output)

   for batch, duration in ingestion_matches:
       ingestion_durations[int(batch)] = int(duration) / 1000000

   for batch, duration in algorithm_matches:
       algorithm_durations[int(batch)] = int(duration) / 1000000

   # print(">> ingestion_durations:", ingestion_durations)
   # print(">> algorithm_durations:", algorithm_durations)

   return ingestion_durations, algorithm_durations

def save_totale2e_to_csv(results, filepath):
    with open(filepath, 'w', newline='') as csvfile:
        fieldnames = ['Graph_Type', 'Hosts', 'Ingestion_Duration', 'Algorithm_Duration']
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)

        writer.writeheader()
        print("results:", results)
        for graph_type in results.keys():
            for hosts in results[graph_type].keys():
                data = results[graph_type][hosts]
                ingestion_duration = data['ingestion'][0] if data['ingestion'] != {} else 0
                algorithm_duration = data['algorithm'][0] if data['algorithm'] != {} else 0
                print("ing:", ingestion_duration, " algo:", algorithm_duration)
                writer.writerow({
                    'Graph_Type': graph_type,
                    'Hosts': hosts,
                    'Ingestion_Duration': ingestion_duration,
                    'Algorithm_Duration': algorithm_duration
                })

def main():
    """
    This script expects the following directory structure.

    e2e_[app_name]_[input_graph]
      |_ [graph_type1]
      |    |_ [num_hosts0]
      |    |  |_ total_e2e.out
      |    |
      |    |_ [num_hosts1]
      |       |_ total_e2e.out
      |
      |_ [graph_type2]
          ...
    """
    parser = argparse.ArgumentParser(description='Run and plot benchmark results based on command line flags.')
    # parser.add_argument('--ingest', action='store_true', help='Plot ingestion durations.')
    parser.add_argument('--output', type=str, help='Output file path to plot.', default=None)

    args = parser.parse_args()

    output_path = args.output
    if not output_path:
        print("No output file path provided. Use --output.")
        return

    # The output_path's last directory name format is [algo]_[input_graph]
    fname = os.path.basename(os.path.normpath(output_path))
    fname_split = fname.split('_')
    assert fname_split[0] == "e2e", " Output directory's prefix should be 'e2e'"
    algo_name = fname_split[1]
    input_name = fname_split[2]

    total_e2e_results = {}
    for graph in os.listdir(output_path):
        total_e2e_results[graph] = {}
        for hosts in os.listdir(output_path+"/"+graph):
            total_e2e_results[graph][hosts] = {}

            # We have interest on e2e execution time per-host or/and all execution.
            # The e2e execution file names are total_e2e.out and host[host number]_e2e.out.
            # We do not parse per-batch results.

            total_e2e_fname = "total_e2e.out"
            log_path = output_path+"/"+graph+"/"+hosts+"/"+total_e2e_fname
            ingestion_durations, algorithm_durations = parse(log_path, algo_name)
            total_e2e_results[graph][hosts] = {
                'ingestion': ingestion_durations,
                'algorithm': algorithm_durations
            }

    save_totale2e_to_csv(total_e2e_results, f"totale2e_{algo_name}_{input_name}.csv")


if __name__ == "__main__":
   main()
