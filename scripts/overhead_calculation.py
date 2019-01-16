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
    parser = argparse.ArgumentParser(description='Generate tables with percentage overhead for given policies.')
    parser.add_argument('-od', '--outputdir', default='./output', help='Directory where output files will be placed')
    parser.add_argument('-id', '--inputdir', default='./data', help='Directory where input are found')
    parser.add_argument('-p', '--policies', action='append', default=[], help='Policies we want to show results of. Options: noPart,criticalAware,criticalAwareV2,dunn.')

    args = parser.parse_args()

    outputPath= os.path.abspath(args.outputdir)

    columns = ['policy','percentageOverhead:mean']

    index = range(0, 12)
    dfTotal = pd.DataFrame(columns=columns, index = index)


    numP = 0
    for p in args.policies:
        print(p)
        # open table file
        wl_name = args.inputdir + "/overhead/" + p + "-times.csv"
        dfp = pd.read_table(wl_name, sep=",")

        # add new column
        dfp["percentageOverhead"] = ( pd.to_numeric(dfp["overhead"]) / 1000000 ) / ( pd.to_numeric(dfp["interval"]) / 2 )
        dfp["percentageOverhead"] = dfp["percentageOverhead"] * 100

        # save modified table file
        dfp.to_csv(wl_name, sep=',')

        # fill total table values
        dfTotal.ix[numP,'policy'] = p
        dfTotal.ix[numP,'percentageOverhead:mean'] = dfp["percentageOverhead"].mean()

        numP = int(numP) + 1

    # Save total table
    outputPathPolicy = outputPath + "/summary-overhead-table.csv"
    dfTotal.to_csv(outputPathPolicy, sep=',')


if __name__ == "__main__":
    main()
