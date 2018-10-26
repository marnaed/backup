
import matplotlib
matplotlib.use('Agg')

import argparse
import numpy as np
import os
import pandas as pd
import re
import scipy.stats
import sys
import yaml
import glob

import matplotlib.pyplot as plt

def main():
    parser = argparse.ArgumentParser(description='Process results of workloads by intervals.')
    parser.add_argument('-w', '--workloads', required=True, help='.yaml file where the list of workloads is found.')
    parser.add_argument('-od', '--outputdir', default='./output', help='Directory where output files will be placed')
    parser.add_argument('-id', '--inputdir', default='./data', help='Directory where input are found')
    parser.add_argument('-p', '--policies', action='append', default=[], help='Policies we want to show results of. Options: noPart,criticalAware.')

    ## EVENTS USED ##
    ev0 = "mem_load_uops_retired.l2_miss"
    ev1 = "mem_load_uops_retired.l3_miss"
    ev2 = "cycle_activity.stalls_ldm_pending"
    ev3 = "intel_cqm/llc_occupancy/"

    Z = 1.96  # value of z at a 95% c.i.
    NUM_REPS_APP = 3  # num of values used to calculate the mean value
    SQRT_NUM_REPS_APP = np.sqrt(NUM_REPS_APP)


    args = parser.parse_args()

    print(args.workloads)

    with open(args.workloads, 'r') as f:
        workloads = yaml.load(f)

        outputPath = os.path.abspath(args.outputdir)

    for wl_id, wl in enumerate(workloads):

        #name of the file with raw data
        wl_show_name = "-".join(wl)

        for policy in args.policies:

            numW = 0
            result = pd.DataFrame()

            # Create output files directory
            outputPathPolicy = outputPath + "/" + policy + "/boxplots"
            os.makedirs(os.path.abspath(outputPathPolicy), exist_ok=True)

            # Dataframe for interval table
            wl_in_path = args.inputdir + "/" + policy + "/data-agg/" + wl_show_name + ".csv"
            print(wl_in_path)
            df = pd.read_table(wl_in_path, sep=",")

            # Calculate MPKI_L3
            relErrIns = (df["instructions:std"] / df["instructions:mean"] )**2
            relErrEv1 = (df[ev1+":std"] / df[ev1+":mean"] )**2
            relErr = np.sqrt(relErrIns + relErrEv1)
            df[ev1+":mean"] = df[ev1+":mean"] / (df["instructions:mean"] / 1000)
            df[ev1+":std"] = df[ev1+":mean"] * relErr
            df[ev1+":std"] = Z * ( df[ev1+":std"] / SQRT_NUM_REPS_APP )
            df = df.rename(columns={ev1+':mean': 'MPKIL3:mean', ev1+':std': 'MPKIL3:ci'})

            df = df.set_index('interval')
            groups = df.groupby(level=[0], as_index=False)

            count = 0;
            tmp = 0;
            for interval, df in groups:
                df = df.rename(columns={'MPKIL3:mean': interval})
                df.reset_index(drop=True, inplace=True)
                result = pd.concat([result, df[interval]], axis=1, ignore_index=True)
                #count = count + 1;
                #if count == 20:
                #    fig = plt.figure()
                #    boxplot = result.boxplot()
                #    finalPath = outputPathPolicy + "/" + wl_show_name + "_" + str(tmp) + "_exec_boxplot.pdf"
                #    plt.savefig(finalPath)
                #    plt.close(fig)
                #    tmp = tmp + 1;
                #    count = 0;


        print(result)
        fig = plt.figure()
        plt.rcParams["figure.figsize"] = [25,9]
        boxplot = result.boxplot()
        finalPath = outputPathPolicy + "/" + wl_show_name + "_exec_boxplot.pdf"
        print(finalPath)
        plt.show()
        plt.savefig(finalPath)
        plt.close(fig)


# El main es crida des d'ací
if __name__ == "__main__":
    main()

