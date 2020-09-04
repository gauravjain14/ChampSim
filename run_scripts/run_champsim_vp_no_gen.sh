#!/bin/bash

CSIM_EXEC="run_champsim.sh"
CSIM_ARGS="bimodal-no-no-no-no-lru-1core 1 30"

#convert CVP trace to Champsim
while [ "$1" != "" ]; do
	case $1 in
		-d | --dir )			shift
								dirname=$1
								;;
		-f | --filename )		shift
								filename=$1
								;;
		-e | --extra_opts )		shift
								extra_opts=$1
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
		echo ${stripped_fname}
		cd "ChampSim"
		xz -dk "dpc3_traces/trace_values_${stripped_fname}.out.xz"
		source ${CSIM_EXEC} ${CSIM_ARGS} "trace_${stripped_fname}.out.xz" "trace_values_${stripped_fname}.out" false ${extra_opts} 
		rm -rf "dpc3_traces/trace_values_${stripped_fname}.out"
		cd ..
	done
elif [ -n "${filename}" -a ! -z "$filename" ]; then
	echo "hello"	
fi



#printf '%s\n' "${filelist[@]}"
