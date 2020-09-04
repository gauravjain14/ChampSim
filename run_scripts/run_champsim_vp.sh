#!/bin/bash

CVP_EXEC="CVP/cvp_champsim_trace_converter"
CVP_TRACE_BASE_DIR="CVP_Converted_Trace"
CSIM_EXEC="run_champsim.sh"
CSIM_ARGS="bimodal-no-no-no-no-lru-1core 1 30"

mkdir -p ${CVP_TRACE_BASE_DIR}

#convert CVP trace to Champsim
while [ "$1" != "" ]; do
	case $1 in
		-d | --dir )			shift
								dirname=$1
								;;
		-f | --filename )		shift
								filename=$1
								;;
		* )						usage
								exit 1
	esac
	shift
done

if [ -n "{$dirname}" -a ! -z "$dirname" ]; then
	filelist=($(ls ${dirname}))
	for f in "${filelist[@]}"
	do
		stripped_fname="$(echo "$f" | cut -f 1 -d '.')"
		./${CVP_EXEC} "$dirname/${f}" "${CVP_TRACE_BASE_DIR}/trace_${stripped_fname}.out" \
										"${CVP_TRACE_BASE_DIR}/trace_values_${stripped_fname}.out"
		#./${EXEC} "$(realpath ${f})" "trace_${stripped_fname}.out" "trace_values_${stripped_fname}.out"
		cp "${CVP_TRACE_BASE_DIR}/trace_${stripped_fname}.out" "ChampSim/dpc3_traces/"
		cp "${CVP_TRACE_BASE_DIR}/trace_values_${stripped_fname}.out" "ChampSim/dpc3_traces/"
		cd "ChampSim"
		xz "dpc3_traces/trace_${stripped_fname}.out"
		source ${CSIM_EXEC} ${CSIM_ARGS} trace_${stripped_fname}.out.xz trace_values_${stripped_fname}.out false
		#rm -rf "dpc3_traces/trace_${stripped_fname}.out.xz"
		#rm -rf "dpc3_traces/trace_values_${stripped_fname}.out"
		xz "dpc3_traces/trace_values_${stripped_fname}.out"
		cd ..
		rm -rf "${CVP_TRACE_BASE_DIR}/trace_${stripped_fname}.out"
		rm -rf "${CVP_TRACE_BASE_DIR}/trace_values_${stripped_fname}.out"
	done
elif [ -n "${filename}" -a ! -z "$filename" ]; then
	echo "hello"	
fi



#printf '%s\n' "${filelist[@]}"
