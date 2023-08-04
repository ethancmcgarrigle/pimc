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
import pandas as pd

## This function runs statistics on the runs accessed (i.e. parameter sweep). Then it collects the relevant data and plots it at the end

def process_sweep(sweep_array, dir_prefix, data_filename, _isCleaning):

  # Loop through the tau directories and run the processing script 
  for i, p_ in enumerate(sweep_array):

  
    outer_dir_name = dir_prefix + '_' + str(p_) 
  
    # In each directory, run pimcave.py to generate avg + std-error  
    path = './' + outer_dir_name + '/OUTPUT/'

    if(_isCleaning):
      print()
      print('Removing old processed files')
      print()
      subprocess.call('rm ' + path + data_filename, shell=True) 

    print('Processing the directory: ')
    print(path)  
    print()
    print()
    # read the gce-estimator filename 
    dirlist = os.listdir(path)
    filenames = [item for item in dirlist if os.path.isfile(os.path.join(path, item) ) ]
    #print(filenames)
    gce_estimator_file = [name for name in filenames if 'gce-estimator' in name]
    gce_estimator_file = gce_estimator_file[0] 
    print()
    print('Processing ' + gce_estimator_file)
    print()

    #python_process_cmd = 'pimcave.py ' + path + gce_estimator_file + ' > ' + path + 'tmp.dat' 
    tail_cmd = 'tail -n +3 > ' 
    # This command will run pimcave on the gce-estimator file in the path and pipe the output to tail, which cuts the first two lines, stores resulting data
    python_process_cmd = 'pimcave.py ' + path + gce_estimator_file + ' | ' + tail_cmd + path + data_filename 
    subprocess.call(python_process_cmd, shell = True) # runs the python command 
  
    
def extract_observables(sweep_array, dir_prefix, data_filename, _isCleaning):

  # -- Function for extracting the observables calculated from pimcave.py (from the process_sweep fxn above) --  

  # Loop through the tau directories, extract the observables  
  sample_data = './' + dir_prefix + '_' + str(sweep_array[0]) + '/OUTPUT/' + data_filename

  # read the first column (observable names) from the sample pimcave output
  get_obs_list_cmd = "cut -d ' ' -f 1 " + sample_data + " > ./observables_list.dat"

  if(_isCleaning):
    subprocess.call('rm ./observables_list.dat', shell=True)

  subprocess.call(get_obs_list_cmd, shell=True)

  print('Observables: ')
  with open('./observables_list.dat', 'r') as f:
    obs_list = [line.strip() for line in f] # strip() removes excess space
    print(obs_list)

  # obs_list contains the list of observables   
  # Create data frames using the observable names as keys for 1) the means and 2) the errors 
  #   - we want a data frame that stores for each obs an array of length(sweep_array) 
  row_key_list = ['means', 'errors']
  pimc_output_df = pd.DataFrame(columns = obs_list, index = row_key_list) 

  for row in row_key_list:
    pimc_output_df.loc[row] = list( np.zeros( (len(obs_list), len(sweep_array) ) ) )

  # Check that the data frame has been set up correctly 
  print(pimc_output_df) 

  # Loop through each directory and load the data frame with the observable data 
  for i, p_ in enumerate(sweep_array):
    outer_dir_name = dir_prefix + '_' + str(p_) + '/OUTPUT' 
  
    # In each directory, run pimcave.py to generate avg + std-error  
    path = './' + outer_dir_name + '/'

    print('- Extracting observables -')
    #cut_cmd = 'cut -d ' ' -f 2- ' + path + data_filename + ' > tmp.dat'  # removes the first column of observable names (strings) 
    cut_cmd = "cut -d ' ' -f 1- " # removes the first column of observable names (strings) 
    #awk_cmd = "awk '{$1=""; sub(/^ +/, ""); $1=$1} 1' OFS=" " tmp.dat > output.txt " # Credit to Chat GPT for this 
    awk_cmd = 'awk \'{ $1=""; sub(/^ +/, ""); $1=$1} 1\' OFS=" "'  # Credit to Chat GPT for this 

    # pipe the output of the cut_cmd into the awk_cmd to format the data into readable columns with 1 space bt. each entry within a row 
    # create a formatted data file:  
    format_data_fname = 'data_formatted.dat'
    format_cmd = cut_cmd + path + data_filename + ' | ' + awk_cmd + ' > ' + path + format_data_fname 
    if(_isCleaning):
      subprocess.call('rm ' + path + format_data_fname, shell=True)

    # run the formatting command 
    subprocess.call(format_cmd, shell = True) # runs the python command 

    # import the data 
    data = np.loadtxt(path + format_data_fname, unpack=True)
    means = data[0]
    errs = data[1]

    # Extract observables
      # jth row of means or errs corresponds to O the observable 
      # ith entry in the dataframe corresponds to the sweep_array (dtau) value  
    for j, O in enumerate(obs_list):
      pimc_output_df.loc['means'][O][i] = means[j] 
      pimc_output_df.loc['errors'][O][i] = errs[j] 


  return pimc_output_df



def plot_observables(sweep_array, obs_dataframe, obs_to_plot, x_axis_label, y_axis_labels):
  # Input is the (dtau) sweep array and the dataframe with means and errors of each observables as a fxn of the sweep variable (dtau) 
  #   "desired_observables" is a list of observables (strings) to plot  
 
  assert(len(obs_to_plot) == len(y_axis_labels)) 
  plt.style.use('/home/emcgarrigle/CSBosonsCpp/tools/Figs_scripts/plot_style_orderparams.txt') 
  for j, O in enumerate(obs_to_plot): 
    fig_filename = O + '_PIMC_tau_sweep.eps'
    plt.figure(figsize=(5.0, 5.0))
    plt.errorbar(sweep_array, obs_dataframe.loc['means'][O], obs_dataframe.loc['errors'][O], marker='o', color = 'b', markersize = 6, linewidth = 0.5, label = 'PIMC') 
    plt.xlabel(x_axis_label, fontsize = 24) 
    #plt.xlabel(r'$\Delta \tau$',fontsize = 20, fontweight = 'bold')
    plt.ylabel(y_axis_labels[j], fontsize = 28) 
    plt.legend()
    #plt.savefig(fig_filename, dpi=300)
    plt.show()



if __name__ == '__main__':
  # -- This function does not return anything -- 
  # -- This function runs a python processing script for a sweep of PIMC runs, with 1 run per directory matching -- 
  # -- Run this function from the parent directory that contains the folder of PIMC runs -- 

  # 1. Specify the sweep array 
  dtau_list = np.array([0.03, 0.02, 0.01, 0.008, 0.006, 0.004, 0.002, 0.001, 0.0005, 0.0001])
  #dtau_list = dtau_list[1:4]

  # 2. Specify the sweep variable string  
  sweep_var = 'dtau'

  # 3. Specify the intermediate data files that store the pimcave.py output  
  data_filename = 'data.dat'

  # 4. Additional script inputs 
  _isCleaning = False  # remove any old processing files (data_filename); this does not delete the raw pimc data 
  _isPlotting = True   # make plots?

  # 5. list any desired observables (using the appropriate strings) and their corresponding y-axis labels for plotting  
  #      - all observables are extracted; these lists are just for plotting 
  desired_observables = ['N', 'E_mu', 'density']
  x_axis_label = r'$\Delta_{\tau}$'
  y_axis_labels = ['$N$', '$U$', r'$\rho$']

  # run the various functions: 
  process_sweep(dtau_list, sweep_var, data_filename, _isCleaning) # run pimcave on all the PIMC runs 
  stats_dframe = extract_observables(dtau_list, sweep_var, data_filename, _isCleaning)  # extract the observables (means and errors) and put them into a data frame 

  if(_isPlotting):
    plot_observables(dtau_list, stats_dframe, desired_observables, x_axis_label, y_axis_labels) 




