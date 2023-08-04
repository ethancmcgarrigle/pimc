import csv
from mpmath import *
import subprocess
import os
import re
import numpy as np
import matplotlib
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt
import pdb
import yaml
import math
## This function runs statistics on the runs accessed (i.e. parameter sweep). Then it collects the relevant data and plots it at the end


dtau = np.array([0.03, 0.02, 0.01, 0.008, 0.006, 0.004, 0.002, 0.001, 0.0005, 0.0001])

for i, dtau_ in enumerate(dtau):

  outer_dir_name = 'dtau_' + str(dtau_) 
  # mkdir command
  str_mkdir = 'mkdir ' + outer_dir_name
  subprocess.call(str_mkdir, shell = True)
  # change directory
  os.chdir(outer_dir_name)

  # copy the submit and inputs and graphing scripts/files 
  submit_script = 'submit.sh'
  cp_submit = 'cp ../' + submit_script + ' ./' + submit_script

  sed_command1 = 'sed -i "s/__dtau__/' + str(dtau_) + '/g" ' + submit_script 

  jobname = 'pimc_dtau_'  + str(dtau)
  sed_command_jobname = 'sed -i "s/__jobname__/' + jobname + '/g" ' + submit_script 
  #qsub_cmd = 'qsub ' + submit_script # for PBS 
  qsub_cmd = 'sbatch ' + submit_script # for slurm
  
  str_list = [cp_submit, sed_command1, sed_command_jobname, qsub_cmd] 

  # execute all the commands in order
  for s in str_list:
    subprocess.call(s, shell = True)

  os.chdir('../')


