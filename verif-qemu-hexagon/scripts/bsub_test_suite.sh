#!/bin/bash

set -x

bsub -R 'select[ubuntu20_llvm] && rusage[mem=12288]' -I ./packet_verif ${*}
