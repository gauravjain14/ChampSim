#!/bin/bash

if [ "$#" -lt 4 ]; then
    echo "Illegal number of parameters"
    echo "Usage: ./run_champsim.sh [BINARY] [N_WARM] [N_SIM] [TRACE] [TRACE_VALUES] [ENABLE_GDB] [EXTRA_OPTS] [OPTION]"
    exit 1
fi

# an extremely non-scalable way of taking arguments from command line. Why ?????
TRACE_DIR=$PWD/dpc3_traces
BINARY=${1}
N_WARM=${2}
N_SIM=${3}
TRACE=${4}
TRACE_VALUES=${5}
USE_GDB=${6:-false}
EXTRA_OPTS=${7}
OPTION=${8}

# Sanity check
if [ -z $TRACE_DIR ] || [ ! -d "$TRACE_DIR" ] ; then
    echo "[ERROR] Cannot find a trace directory: $TRACE_DIR"
    exit 1
fi

if [ ! -f "bin/$BINARY" ] ; then
    echo "[ERROR] Cannot find a ChampSim binary: bin/$BINARY"
    exit 1
fi

re='^[0-9]+$'
if ! [[ $N_WARM =~ $re ]] || [ -z $N_WARM ] ; then
    echo "[ERROR]: Number of warmup instructions is NOT a number" >&2;
    exit 1
fi

re='^[0-9]+$'
if ! [[ $N_SIM =~ $re ]] || [ -z $N_SIM ] ; then
    echo "[ERROR]: Number of simulation instructions is NOT a number" >&2;
    exit 1
fi

if [ ! -f "$TRACE_DIR/$TRACE" ] ; then
    echo "[ERROR] Cannot find a trace file: $TRACE_DIR/$TRACE"
    exit 1
fi

if [ ! -f "$TRACE_DIR/$TRACE_VALUES" ] ; then
    echo "[ERROR] Cannot find a trace file: $TRACE_DIR/$TRACE_VALUES"
    exit 1
fi

if [ -z $EXTRA_OPTS ]; then
    RESULTS_DIR="results_${N_SIM}M"
else
    RESULTS_DIR="results_${EXTRA_OPTS}_${N_SIM}M"
fi

mkdir -p ${RESULTS_DIR}

if [ $USE_GDB = false ]
then
	(./bin/${BINARY} -warmup_instructions ${N_WARM}000000 -simulation_instructions ${N_SIM}000000 ${OPTION} -trace_values ${TRACE_DIR}/${TRACE_VALUES} -traces ${TRACE_DIR}/${TRACE}) &> ${RESULTS_DIR}/${TRACE}-${BINARY}${OPTION}.txt
else
	gdb --args ./bin/${BINARY} -warmup_instructions ${N_WARM}000000 -simulation_instructions ${N_SIM}000000 ${OPTION} -trace_values ${TRACE_DIR}/${TRACE_VALUES} -traces ${TRACE_DIR}/${TRACE}
fi
