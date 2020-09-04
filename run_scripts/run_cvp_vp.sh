#!/bin/bash -x

EXEC="CVP/cvp"
CVP_TRACE_BASE_DIR="VP_Trace_Results"
RESULTS_DIR="CVP/${CVP_TRACE_BASE_DIR}"

mkdir -p ${RESULTS_DIR}

usage() { echo "Usage: [-f <fetch-width>] [-t <trace-dir-name>] [-d <perfect-cache>] [-v <vp-enabled>] [-p <perfect-vp-enabled>]" 1&>2; exit 1;}

while getopts ":f:t:v:p:d::" o; do
    case "${o}" in
        f)
			fetch_width=${OPTARG}
            ;;
        t)
            dirname=${OPTARG}
            ;;
        v)
			if [ ${OPTARG} -eq 1 ]; then
	            enable_vp="-v"
			fi
            ;;
		p)
			if [ ${OPTARG} -eq 1 ]; then
				perfect_vp="-p"
			fi
			;;
        d)
            if [ ${OPTARG} -eq 1 ]; then
                perfect_cache="-d"
            fi
            ;;
        *)
            usage
            ;;
    esac
done

extra_opts=""
if [ -n "${perfect_cache}" -a ! -z "${perfect_cache}" ]; then
	extra_opts="${extra_opts}_pc"
fi

if [ -n "${enable_vp}" -a ! -z "${enable_vp}" ]; then
	extra_opts="${extra_opts}_vp"
fi

filelist=($(ls ${dirname}))
for f in "${filelist[@]}"
do
	stripped_fname="$(echo "$f" | cut -f 1 -d '.')"
	./${EXEC} -F "${fetch_width},0,0,0,0" ${perfect_cache} ${enable_vp} ${perfect_vp} "$dirname/${f}" >> "${RESULTS_DIR}/trace${extra_opts}_${stripped_fname}.log"
done

#printf '%s\n' "${filelist[@]}"
