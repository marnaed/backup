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
    parser.add_argument('-p', '--policies', action='append', default=[], help='Policies we want to show results of.         Options: np,hg.')
    args = parser.parse_args()
    Z = 1.96  # value of z at a 95% c.i.
    NUM_REPS_APP = 3  # num of values used to calculate the mean value
    SQRT_NUM_REPS_APP = np.sqrt(NUM_REPS_APP)
    NUM_REPS_APP_2 = 6
    SQRT_NUM_REPS_APP_2 = np.sqrt(NUM_REPS_APP_2)
    numApps = 8

    ## EVENTS USED ##
    ev0 = "mem_load_uops_retired.l3_hit"
    ev1 = "mem_load_uops_retired.l3_miss"
    ev2 = "cycle_activity.stalls_ldm_pending"
    ev3 = "intel_cqm/llc_occupancy/"

    with open(args.workloads, 'r') as f:
        workloads = yaml.load(f)

    outputPath = os.path.abspath(args.outputdir)
    os.makedirs(os.path.abspath(outputPath), exist_ok=True)

    for wl_id, wl in enumerate(workloads):
            wl_show_name = "-".join(wl)
            print(wl_show_name)
            numConfig = 0
            for policy in args.policies:

                outputPathPolicy = outputPath + "/" + policy + "/resultTables"
                os.makedirs(os.path.abspath(outputPathPolicy), exist_ok=True)

                # dataframe for interval table
                wl_in_path = args.inputdir + "/" + policy + "/data-agg/" + wl_show_name + ".csv"
                dfWay = pd.read_table(wl_in_path, sep=",")
                dfWay = dfWay[["interval","app","cycles:mean","cycles:std","instructions:mean","instructions:std","ipc:mean","ipc:std",ev0+":mean",ev0+":std",ev1+":mean",ev1+":std",ev2+":mean",ev2+":std"]]

                # calculate confidence interval of IPC
                dfWay["ipc:std"] =  Z * ( dfWay["ipc:std"] / SQRT_NUM_REPS_APP )

                # calculate confidence interval of Memory Stalls
                dfWay[ev2+":std"] = Z * ( dfWay[ev2+":std"] / SQRT_NUM_REPS_APP )

                # rename columns
                dfWay = dfWay.rename(columns={'ipc:mean': 'IPC:mean', 'ipc:std': 'IPC:ci'})

                # calculate Hit ratio
                access = dfWay[ev0+":mean"] + dfWay[ev1+":mean"]
                relAccess = dfWay[ev0+":std"] + dfWay[ev1+":std"]
                relErrHit = (dfWay[ev0+":std"] / dfWay[ev0+":mean"] )**2
                relErrAcc = (relAccess / access)**2
                relErr = np.sqrt(relErrHit + relErrAcc)
                dfWay[ev0+":mean"] = dfWay[ev0+":mean"] / access
                dfWay[ev0+":std"] = dfWay[ev0+":mean"] * relErr
                dfWay[ev0+":std"] = Z * ( dfWay[ev0+":std"] / SQRT_NUM_REPS_APP )

                # calculate MPKI-LLC
                relErrIns = (dfWay["instructions:std"] / dfWay["instructions:mean"] )**2
                relErrEv1 = (dfWay[ev1+":std"] / dfWay[ev1+":mean"] )**2
                relErr = np.sqrt(relErrIns + relErrEv1)
                dfWay[ev1+":mean"] = dfWay[ev1+":mean"] / (dfWay["instructions:mean"] / 1000)
                dfWay[ev1+":std"] = dfWay[ev1+":mean"] * relErr
                dfWay[ev1+":std"] = Z * ( dfWay[ev1+":std"] / SQRT_NUM_REPS_APP )
                dfWay = dfWay.rename(columns={ev1+':mean': 'MPKIL3:mean', ev1+':std': 'MPKIL3:ci', ev0+':mean': 'HitRatioL3:mean', ev0+':std': 'HitRatioL3:ci',ev2+':mean': 'MemoryStalls:mean', ev2+':std': 'MemoryStalls:ci'})

                # GENERATE Interval tables app per workload
                dfWay = dfWay[["interval","app","IPC:mean","IPC:ci","HitRatioL3:mean","HitRatioL3:ci","MPKIL3:mean","MPKIL3:ci","MemoryStalls:mean","MemoryStalls:ci"]]

                dfWayAux = dfWay
                dfWay = dfWay.set_index(['interval','app'])
                groups = dfWay.groupby(level=[1])

                for appName, df in groups:
                    #generate interval_data_table
                    outputPathWaysWorkload = outputPathPolicy + "/" + wl_show_name
                    os.makedirs(os.path.abspath(outputPathWaysWorkload), exist_ok=True)
                    outputPathApp = outputPathWaysWorkload + "/" + appName + "-intervalDataTable.csv"
                    df.to_csv(outputPathApp, sep=',')

                groups = dfWay.groupby(level=[0])
                columns = ['interval','IPC:mean','IPC:ci',"HitRatioL3:mean","HitRatioL3:ci",'MPKIL3:mean','MPKIL3:ci',"MemoryStalls:mean","MemoryStalls:ci"]
                numRows = dfWay['IPC:mean'].count()/numApps
                numRows = np.asscalar(np.int16(numRows))
                index = range(0, numRows + 1)

                # GENERATE tables with total, mean and median values
                dfVals = ['dfTotWays', 'dfMeanWays', 'dfMedianWays']

                for dfv in dfVals:
                    dfv = pd.DataFrame(columns=columns, index = index)
                    dfv['interval'] = range(0, numRows + 1)
                    dfv = dfv.set_index(['interval'])

                    for interval, df in groups:
                        dfv.ix[interval, 'IPC:mean'] = df['IPC:mean'].sum()
                        dfv.ix[interval, 'IPC:ci'] = np.sqrt((df['IPC:ci']**2).sum())
                        dfv.ix[interval, 'MPKIL3:mean'] = df['MPKIL3:mean'].sum()
                        dfv.ix[interval, 'MPKIL3:ci'] = np.sqrt((df['MPKIL3:ci']**2).sum())
                        dfv.ix[interval, 'HitRatioL3:mean'] = df['HitRatioL3:mean'].sum()
                        dfv.ix[interval, 'HitRatioL3:ci'] = np.sqrt((df['HitRatioL3:ci']**2).sum())
                        dfv.ix[interval, 'MemoryStalls:mean'] = df['MemoryStalls:mean'].sum()
                        dfv.ix[interval, 'MemoryStalls:ci'] = np.sqrt((df['MemoryStalls:ci']**2).sum())

                    outputPathv = outputPathPolicy + "/" + wl_show_name + "-" + dfv + ".csv"
                    dfv.to_csv(outputPathv, sep=',')


# El main es crida des d'ací
if __name__ == "__main__":
    main()

