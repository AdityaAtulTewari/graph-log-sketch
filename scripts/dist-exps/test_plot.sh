#!/bin/bash

E2E_OUTPUT_DIR=$1
PPOLICY_OUTPUT_DIR=$2

python total_e2e_csv.py --output $E2E_OUTPUT_DIR
Rscript totale2e_plot.R totale2e_bfs-pull_test.csv sample_totale2e_bfs-pull_test.pdf

python perhost_partitioning_csv.py --output $PPOLICY_OUTPUT_DIR
Rscript ppolicy_plot.R 4 ppolicy_bfs-pull_test.csv sample_perhost_bfs-pull_test.pdf
