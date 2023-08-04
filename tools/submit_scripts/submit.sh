#!/bin/bash
#SBATCH --nodes=1 --ntasks-per-node=1
#SBATCH --time=1000:00:00
#SBATCH --job-name=__pimc_test_
######################################
inputfile=input.yml
outputfile=output.out
pimcdir=~/pimc_2D/pimc/build
######################################

cd $SLURM_SUBMIT_DIR
outdir=${SLURM_SUBMIT_DIR}
rundir=${outdir}
username=`emcgarrigle`

############# TO USE LOCAL SCRATCH FOR INTERMEDIATE IO, UNCOMMENT THE FOLLOWING
#if [ ! -d /scratch_local/${username} ]; then
#  rundir=/scratch_local/${username}/${PBS_JOBID}
#  mkdir -p $rundir
#  cp ${PSB_O_WORKDIR}/* $rundir
#  cd $rundir
#fi
#####################################################

cat $SLURM_JOB_NODELIST > nodes

# Run the job
srun ${pimcdir}/pimc.e -T 5 -N 16 -n 0.02198 -t 0.01 -M 8 -C 1.0 -I delta -g 1.0 -X free -E 10000 -S 20 -l 7 -u 0.02 --relax             # ${inputfile} > ${outdir}/${outputfile}
# Copy back results
if [ "$rundir" != "$outdir" ]; then
  mv * ${outdir}
fi

# Force good exit code here - e.g., for job dependency
exit 0
