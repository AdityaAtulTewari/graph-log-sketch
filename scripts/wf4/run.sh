#!/bin/bash

# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2023. University of Texas at Austin. All rights reserved.

TACC="${TACC:-}"
HOST_THREADS="${HOST_THREADS:-32}"

HOSTS="${HOSTS:-1}"
PROCS="${PROCS:-4}"
TIME="${TIME:-0:15:00}"
QUEUE="${QUEUE:-normal}"
BUILD="${BUILD:-build}"
DATA="${DATA:-data}"
JOBS="${JOBN:-wf4-run}"
OUTS="${OUTS:-wf4}"

THREADD=$((${HOST_THREADS} * ${HOSTS} / ${PROCS}))
THREADS="${THREADS:-${THREADD}}"
K="${K:-10}"
GRAPH_DIR="${GRAPH_DIR:-graphs/wf4/scale-2}"
EPOCHS="${EPOCHS:-10}"
GRAPH_NAME="${GRAPH_NAME:-scale-2}"
RRR_SETS="${RRR_SETS:-10000}"
INFLUENTIAL_THRESHOLD="${INFLUENTIAL_THRESHOLD:-0}"
SEED="${SEED:-9801}"
BIND="${BIND:-hwthread}"

JOBN=${DATA}/${JOBS}_${HOSTS}_${PROCS}_${THREADS}_${GRAPH_NAME}_${K}_${EPOCHS}_${RRR_SETS}_${SEED}
OUTF=${DATA}/${OUTS}_${HOSTS}_${PROCS}_${THREADS}_${GRAPH_NAME}_${K}_${EPOCHS}_${RRR_SETS}_${SEED}
ENV=/scratch/09601/pkenney/pando-workflow4-galois/scripts/tacc_env.sh

echo $HOSTS
echo $PROCS
echo $THREADS
echo $TIME
echo $GRAPH_DIR
echo $DATA
echo $QUEUE
echo $JOBN
echo $OUTF

if [ ! -z "${TACC}" ]; then
	sbatch <<EOT
#!/bin/bash

#SBATCH -J ${JOBN}
#SBATCH -o ${JOBN}.out
#SBATCH -e ${JOBN}.err
#SBATCH -t ${TIME}
#SBATCH -N ${HOSTS}
#SBATCH -n ${PROCS}
#SBATCH --mail-type=all
#SBATCH --mail-user=pkenney@utexas.edu
#SBATCH -p ${QUEUE}

module purge
. ${ENV}

export OMP_NUM_THREADS=${THREADS}

ibrun -- ${BUILD}/wf4/wf4 -t ${THREADS} -k ${K} -d ${GRAPH_DIR} \
  -e ${EPOCHS} -r ${RRR_SETS} -s ${SEED}

EOT
else
	mpirun -np ${PROCS} --bind-to ${BIND} --cpus-per-rank ${THREADS} --use-hwthread-cpus --tag-output \
		-- ${BUILD}/wf4/wf4 -t ${THREADS} -k ${K} -d ${GRAPH_DIR} \
		-e ${EPOCHS} -r ${RRR_SETS} -s ${SEED} | tee ${OUTF}
fi
