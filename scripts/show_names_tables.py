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
    parser.add_argument('-n', '--names', action='append', default=[], help='Names of columns we want to generate tables')
    args = parser.parse_args()

    # dictionary with names of headers of data we want to include in the tables
    dictNames = {}

    # policies used
    policies = ['noPart', 'criticalAware']

    # include names in dictionary
    for name in args.names:
        dictNames[name] = "df_"+name

    with open(args.workloads, 'r') as f:
        workloads = yaml.load(f)

    outputPath= os.path.abspath(args.outputdir)

    columns = ['Workload_ID','Workload']
    for p in policies:
    	columns.append(p)

    index = range(0, 34)

    # create data frame for each name
    for name in args.names:
        df = dictNames[name]
        df = pd.DataFrame(columns=columns, index = index)
        df['Workload_ID'] = range(1, 35)
        dictNames[name] = df

    # add values
    for policy in policies:
        numW = 0
        for wl_id, wl in enumerate(workloads):

            #name of the file with raw data
            wl_show_name = "-".join(wl)

            for name in args.names:
                df = dictNames[name]
                df.ix[numW,'Workload'] = wl_show_name

                wl_name = args.inputdir + "/" + policy +  "/data-agg/" + wl_show_name + "_tot.csv"
                dfworkload = pd.read_table(wl_name, sep=",")

                df.ix[numW,policy] = dfworkload[name+':mean'].max()
                dictNames[name] = df

            numW = int(numW) + 1


    # generate tables
    for name in args.names:

        df = dictNames[name]
        show_name = name.replace("/", "-")
        if name == "ipc":
            df["%gain"] = ((df["criticalAware"] / df["noPart"]) - 1) * 100
        else:
            df["%gain"] = ((df["noPart"] / df["criticalAware"]) - 1) * 100

        print(name)
        print(df["%gain"].mean())
        print(df["%gain"].max())
        print(" ")


        # save tables
        df  = df.set_index(['Workload_ID'])
        outputPathPolicy = outputPath + "/" + show_name + "table.csv"
        df.to_csv(outputPathPolicy, sep=',')
        dictNames[name] = df


if __name__ == "__main__":
    main()
