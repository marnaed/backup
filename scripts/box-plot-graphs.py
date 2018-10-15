
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

    args = parser.parse_args()

    print(args.workloads)

    with open(args.workloads, 'r') as f:
        workloads = yaml.load(f)

    outputPath= os.path.abspath(args.outputdir)
    os.makedirs(os.path.abspath(outputPath), exist_ok=True)

    for wl_id, wl in enumerate(workloads):

        #name of the file with raw data
        wl_show_name = "-".join(wl)

        numW = 0
        result = pd.DataFrame()

        for ways in range(1,21):

            wl_name = args.inputdir +  "/" + str(ways) + "w/resultTables/" + wl_show_name + "/00_" + wl_show_name  + "-intervalDataTable.csv"
            #print(wl_name)

            df = pd.read_table(wl_name, sep=",")
            df = df.rename(columns={'MPKIL3:mean': str(ways)+'w'})
            result = pd.concat([result, df[str(ways)+'w']], axis=1)

        fig = plt.figure()
        boxplot = result.boxplot()
        plt.savefig(outputPath + "/" + wl_show_name + "_ways_boxplot.pdf")
        plt.close(fig)


# El main es crida des d'ací
if __name__ == "__main__":
    main()

