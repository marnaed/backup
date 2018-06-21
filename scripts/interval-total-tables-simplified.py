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

            list_ways = args.policies

            numConfig = 0
            for policy in args.policies:

                outputPathPolicy = outputPath + "/" + policy + "/resultTables"
                os.makedirs(os.path.abspath(outputPathPolicy), exist_ok=True)

                # Dataframe for interval table
                wl_in_path = args.inputdir + "/" + policy + "/data-agg/" + wl_show_name + ".csv"
                print(wl_in_path)

                dfWay = pd.read_table(wl_in_path, sep=",")
                dfWay = dfWay[["interval","app","instructions:mean","instructions:std","ipc:mean","ipc:std",ev1+":mean",ev1+":std",ev3+":mean",ev3+":std"]]

                # Calculate confidence interval of IPC
                dfWay["ipc:std"] =  Z * ( dfWay["ipc:std"] / SQRT_NUM_REPS_APP )

                # Calculate LLC occupancy
                dfWay[ev3+":mean"] = (dfWay[ev3+":mean"] / 1024) / 1024
                dfWay[ev3+":std"] = (dfWay[ev3+":std"] / 1024) / 1024
                dfWay[ev3+":std"] = Z * ( dfWay[ev3+":std"] / SQRT_NUM_REPS_APP )

                # Rename columns
                dfWay = dfWay.rename(columns={'ipc:mean': 'IPC:mean', 'ipc:std': 'IPC:ci', ev3+':mean': 'l3_Mbytes_occ:mean', ev3+':std': 'l3_Mbytes_occ:ci'})

                # Calculate MPKI_LLC
                relErrIns = (dfWay["instructions:std"] / dfWay["instructions:mean"] )**2
                relErrEv1 = (dfWay[ev1+":std"] / dfWay[ev1+":mean"] )**2
                relErr = np.sqrt(relErrIns + relErrEv1)
                dfWay[ev1+":mean"] = dfWay[ev1+":mean"] / (dfWay["instructions:mean"] / 1000)
                dfWay[ev1+":std"] = dfWay[ev1+":mean"] * relErr
                dfWay[ev1+":std"] = Z * ( dfWay[ev1+":std"] / SQRT_NUM_REPS_APP )
                dfWay = dfWay.rename(columns={ev1+':mean': 'MPKIL3:mean', ev1+':std': 'MPKIL3:ci'})

                # Interval tables app per workload
                dfWay = dfWay[["interval","app","IPC:mean","IPC:ci","l3_Mbytes_occ:mean","l3_Mbytes_occ:ci","MPKIL3:mean","MPKIL3:ci"]]

                dfWayAux = dfWay
                dfWay = dfWay.set_index(['interval','app'])
                groups = dfWay.groupby(level=[1])

                for appName, df in groups:
                    # Generate interval_data_table for each app
                    outputPathWaysWorkload = outputPathPolicy + "/" + wl_show_name
                    os.makedirs(os.path.abspath(outputPathWaysWorkload), exist_ok=True)
                    outputPathApp = outputPathWaysWorkload + "/" + appName + "-intervalDataTable.csv"
                    df.to_csv(outputPathApp, sep=',')

                # Generate interval data table with agregated values of the apps
                groups = dfWay.groupby(level=[0])
                columns = ['interval','IPC:mean','IPC:ci','MPKIL3:mean','MPKIL3:ci']
                numRows = dfWay['IPC:mean'].count()/numApps
                numRows = np.asscalar(np.int16(numRows))
                index = range(0, numRows + 1)
                dfTotWays = pd.DataFrame(columns=columns, index = index)
                dfTotWays['interval'] = range(0, numRows + 1)
                dfTotWays = dfTotWays.set_index(['interval'])
                for interval, df in groups:
                    #dfTotWays.ix[interval, 'interval'] = interval
                    dfTotWays.ix[interval, 'IPC:mean'] = df['IPC:mean'].sum()
                    dfTotWays.ix[interval, 'IPC:ci'] = np.sqrt((df['IPC:ci']**2).sum())
                    dfTotWays.ix[interval, 'MPKIL3:mean'] = df['MPKIL3:mean'].sum()
                    dfTotWays.ix[interval, 'MPKIL3:ci'] = np.sqrt((df['MPKIL3:ci']**2).sum())

                outputPathTotWays = outputPathPolicy + "/" + wl_show_name + "-total-table.csv"
                dfTotWays.to_csv(outputPathTotWays, sep=',')

# El main es crida des d'ací
if __name__ == "__main__":
    main()

