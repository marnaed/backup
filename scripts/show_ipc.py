import argparse
import numpy as np
import os
import pandas as pd
import re
import scipy.stats
import sys
import yaml
import glob

def main():
    parser = argparse.ArgumentParser(description='Process results of workloads by intervals.')
    parser.add_argument('-w', '--workloads', required=True, help='.yaml file where the list of workloads is found.')
    parser.add_argument('-od', '--outputdir', default='./output', help='Directory where output files will be placed')
    parser.add_argument('-id', '--inputdir', default='./data', help='Directory where input are found')
    parser.add_argument('-p', '--policies', action='append', default=[], help='Policies we want to show results of.')

    args = parser.parse_args()

    NUM_APPS = 8;

    print(args.workloads)

    with open(args.workloads, 'r') as f:
        workloads = yaml.load(f)

    outputPath= os.path.abspath(args.outputdir)

    # create Data Frame with value of Turn Around time for each workload in each policy
    columns = ['Workload_ID','Workload']

    for p in args.policies:
    	columns.append(p)


    columns.append('WeightedSpeedup')
    print("columns : ",columns)
    #print("Num workloads: ",len(args.workloads))
    #index = range(0, len(args.workloads))
    l = args.workloads
    print("Num workloads: ",len(args.workloads))
    index = range(0, 34)
    df = pd.DataFrame(columns=columns, index = index)
    #df['Workload_ID'] = range(1, len(args.workloads)+1)
    df['Workload_ID'] = range(1, 35)


    for policy in args.policies:

        numW = 0
        for wl_id, wl in enumerate(workloads):

            #name of the file with raw data
            wl_show_name = "-".join(wl)

            df.ix[numW,'Workload'] = wl_show_name

            wl_name = args.inputdir + "/" + policy +  "/data-agg/" + wl_show_name + "_tot.csv"
            #print(wl_name)

            #create dataframe from raw data
            dfworkload = pd.read_table(wl_name, sep=",")

            df.ix[numW,policy] = dfworkload['ipc:mean'].sum()


            #calculate weighted speedup
            wl_linux = args.inputdir + "/linux/data-agg/" + wl_show_name + "_tot.csv"
            dflinux = pd.read_table(wl_linux, sep=",")
            wl_ca = args.inputdir + "/criticalAlone/data-agg/" + wl_show_name + "_tot.csv"
            dfca = pd.read_table(wl_ca, sep=",")
            dfca['ipc:mean'] = dfca['ipc:mean'] / dflinux['ipc:mean']
            df.ix[numW, 'WeightedSpeedup'] = dfca['ipc:mean'].sum() / NUM_APPS;


            numW = int(numW) + 1

    # save table
    df = df.set_index(['Workload_ID'])
    outputPathPolicy = outputPath + "/ipc-table.csv"
    print(df)
    df.to_csv(outputPathPolicy, sep=',')


# El main es crida des d'ací
if __name__ == "__main__":
    main()
