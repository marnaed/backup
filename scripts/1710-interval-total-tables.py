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
    #ev0 = "mem_load_uops_retired.l3_hit"
    #ev1 = "mem_load_uops_retired.l3_miss"
    #ev2 = "cycle_activity.stalls_ldm_pending"
    #ev3 = "intel_cqm/llc_occupancy/"


    ev0 = "MEM_LOAD_UOPS_RETIRED.L3_HIT"
    ev1 = "MEM_LOAD_UOPS_RETIRED.L3_MISS"
    ev3 = "l3_kbytes_occ"

    with open(args.workloads, 'r') as f:
        workloads = yaml.load(f)

    outputPath = os.path.abspath(args.outputdir)
    os.makedirs(os.path.abspath(outputPath), exist_ok=True)

    for wl_id, wl in enumerate(workloads):

            wl_show_name = "-".join(wl)
            print(wl_show_name)

            list_ways = args.policies

            #create total dataframe
            columns = ['policy', 'IPC:mean', 'IPC:ci', 'MPKIL3:mean', 'MPKIL3:ci','STP:mean', 'STP:ci', 'ANTT:mean', 'ANTT:ci', 'Unfairness:mean', 'Unfairness:ci', 'Tt:mean', 'Tt:ci']
            index = range(0, len(list_ways))
            dfTotal = pd.DataFrame(columns=columns, index = index)

            numConfig = 0
            for policy in args.policies:

                outputPathPolicy = outputPath + "/" + policy + "/resultTables"
                os.makedirs(os.path.abspath(outputPathPolicy), exist_ok=True)

                # total values of execution
                dfTotal.ix[numConfig,'policy'] = policy

                wl_in_path = args.inputdir + "/" + policy + "/data-agg/" + wl_show_name + "_fin.csv"

                #print(wl_in_path)

                dfaux = pd.read_table(wl_in_path, sep=",")

                dfaux = dfaux[["instructions:mean","instructions:std","ipc:mean","ipc:std",ev1+":mean",ev1+":std"]]


                dfTotal.ix[numConfig,'IPC:mean'] = dfaux["ipc:mean"].sum()
                dfTotal.ix[numConfig,'IPC:ci'] = Z * ((np.sqrt((dfaux["ipc:std"]*dfaux["ipc:std"]).sum())) / SQRT_NUM_REPS_APP)

                #print("dfTotal with IPC")
                #print(dfTotal)

                relErrIns = (dfaux["instructions:std"] / dfaux["instructions:mean"] )**2
                relErrEv1 = (dfaux[ev1+":std"] / dfaux[ev1+":mean"] )**2
                relErr = np.sqrt(relErrIns + relErrEv1)
                dfaux[ev1+":mean"] = dfaux[ev1+":mean"] / (dfaux["instructions:mean"] / 1000)
                dfaux[ev1+":std"] = dfaux[ev1+":mean"] * relErr
                dfaux[ev1+":std"] = Z * ( dfaux[ev1+":std"] / SQRT_NUM_REPS_APP )

                dfTotal.ix[numConfig,'MPKIL3:mean'] = dfaux[ev1+":mean"].sum()
                dfTotal.ix[numConfig,'MPKIL3:ci'] =np.sqrt((dfaux[ev1+":std"]*dfaux[ev1+":std"]).sum())

                #print("dfTotal with MPKIL3")
                #print(dfTotal)

                # dataframe for interval table
                wl_in_path = args.inputdir + "/" + policy + "/data-agg/" + wl_show_name + ".csv"
                #print(wl_in_path)

                dfWay = pd.read_table(wl_in_path, sep=",")
                dfWay = dfWay[["interval","app","instructions:mean","instructions:std","ipc:mean","ipc:std",ev1+":mean",ev1+":std",ev3+":mean",ev3+":std"]]
                #dfWay = dfWay.set_index(['interval'])
                #groups = dfWay.groupby(level=[0])


                # calculate confidence interval of IPC
                dfWay["ipc:std"] =  Z * ( dfWay["ipc:std"] / SQRT_NUM_REPS_APP )

                # calculate l3_kbytes_occ in mbytes
                dfWay[ev3+":mean"] = dfWay[ev3+":mean"] / 1024
                dfWay[ev3+":std"] = dfWay[ev3+":std"] / 1024
                # calculate confidence interval of l3_kbytes_occ
                dfWay[ev3+":std"] = Z * ( dfWay[ev3+":std"] / SQRT_NUM_REPS_APP )

                # rename columns
                dfWay = dfWay.rename(columns={'ipc:mean': 'IPC:mean', 'ipc:std': 'IPC:ci', ev3+':mean': 'l3_Mbytes_occ:mean', ev3+':std': 'l3_Mbytes_occ:ci'})

                # calculate MPKI
                relErrIns = (dfWay["instructions:std"] / dfWay["instructions:mean"] )**2
                relErrEv1 = (dfWay[ev1+":std"] / dfWay[ev1+":mean"] )**2
                relErr = np.sqrt(relErrIns + relErrEv1)

                dfWay[ev1+":mean"] = dfWay[ev1+":mean"] / (dfWay["instructions:mean"] / 1000)
                dfWay[ev1+":std"] = dfWay[ev1+":mean"] * relErr
                dfWay[ev1+":std"] = Z * ( dfWay[ev1+":std"] / SQRT_NUM_REPS_APP )
                dfWay = dfWay.rename(columns={ev1+':mean': 'MPKIL3:mean', ev1+':std': 'MPKIL3:ci'})

                # GENERATE Interval tables app per workload
                dfWay = dfWay[["interval","app","IPC:mean","IPC:ci","l3_Mbytes_occ:mean","l3_Mbytes_occ:ci","MPKIL3:mean","MPKIL3:ci"]]

                dfWayAux = dfWay
                dfWay = dfWay.set_index(['interval','app'])
                groups = dfWay.groupby(level=[1])

                # create DF for LLC of all apps in workload
                columns = ['interval']
                #print(numApps)
                #print(dfWay)
                numRows = dfWay['IPC:mean'].count()/numApps
                numRows = np.asscalar(np.int16(numRows))
                index = range(0, numRows)
                dfLLC = pd.DataFrame(columns=columns, index = index)
                dfLLC["interval"] = range(0, numRows)

                LLCDATAF = dfLLC

                totalmeanLLC = 0;

                for appName, df in groups:
                    #print(appName)

                    #generate interval_data_table
                    outputPathWaysWorkload = outputPathPolicy + "/" + wl_show_name
                    os.makedirs(os.path.abspath(outputPathWaysWorkload), exist_ok=True)
                    outputPathApp = outputPathWaysWorkload + "/" + appName + "-intervalDataTable.csv"
                    df.to_csv(outputPathApp, sep=',')

                    #add column to LLC_occup dataframe
                    #nameC = "LLC" + app
                    #print(list(dfWayAux.columns.values))
                    #print(numRows)
                    AuxAPP = dfWayAux[dfWayAux.app.isin([appName])]
                    AuxAPP = AuxAPP['l3_Mbytes_occ:mean']
                    AuxAPP.index = range(0, numRows)
                    #print(AuxAPP)
                    LLCDATAF[appName] = AuxAPP
                    totalmeanLLC = totalmeanLLC + AuxAPP.mean()

                # save LLC_occup dataframe
                #print(LLCDATAF)
                print(policy + ": " + wl_show_name + " has mean LLC occup: " + str(totalmeanLLC))
                LLCDATAF = LLCDATAF.set_index(['interval'])
                outputPathLLC = outputPathWaysWorkload + "/LLC_occup_apps_data_table.csv"
                LLCDATAF.to_csv(outputPathLLC, sep=',')


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

                # df with individual values to calculate ANTT and STP
                wl_in_path = args.inputdir + "/" + policy + "/data-agg/" + wl_show_name + "_fin.csv"

                dfway = pd.read_table(wl_in_path, sep=",")
                dfway = dfway.set_index(['app'])
                groups = dfway.groupby(level=[0])

                # dataframe for individual execution
                dfIndiv = pd.DataFrame()

                progress = 0
                slowdown = 0

                progressERR = 0
                slowdownERR = 0

                core = 0

                # calculate the progress and slowdown of each app
                # and add all the values
                for app, df in groups:
                    words = app.split('_')
                    #if len(words) == 3:
                    #    appName = words[1] + "_" + words[2]
                    #else:
                    appName = words[1]
                    #print(appName)

                    #wl_indiv = "./data-agg/" + appName + "_fin.csv"
                    wl_indiv = "/home/lupones/manager/experiments/individual/20w/data-agg/" + appName + "_fin.csv"
                    dfIndiv = pd.read_table(wl_indiv, sep=",")
                    progressApp = df.ix[0,'ipc:mean'] / dfIndiv.ix[0,'ipc:mean']
                    slowdownApp = dfIndiv.ix[0,'ipc:mean'] / df.ix[0,'ipc:mean']

                    progress = progress + progressApp
                    slowdown = slowdown + slowdownApp

                    relErrComp = (df.ix[0,'ipc:std'] / df.ix[0,'ipc:mean'])**2
                    relErrIndiv = (dfIndiv.ix[0,'ipc:std'] / dfIndiv.ix[0,'ipc:mean'])**2
                    relERR = np.sqrt(relErrComp + relErrIndiv)

                    progressERR = progressERR + (progressApp*relERR)
                    slowdownERR = slowdownERR + (slowdownApp*relERR)
                    core = core + 1

                # calculate and add values of configuration to total table
                dfTotal.ix[numConfig,'STP:mean'] = progress
                dfTotal.ix[numConfig,'STP:ci'] = Z * (progressERR / 24)

                mean_slowdown = (slowdown / 8)
                unfairness = slowdownERR / mean_slowdown

                unERR = (slowdownERR/8) / mean_slowdown
                unfairnessERR = unfairness*unERR

                dfTotal.ix[numConfig,'Unfairness:mean'] = unfairness
                dfTotal.ix[numConfig,'Unfairness:ci'] = Z * (unfairnessERR / 24)
                dfTotal.ix[numConfig,'ANTT:mean'] = mean_slowdown
                dfTotal.ix[numConfig,'ANTT:ci'] = Z * (unfairness / 24)

                # calculate the Workload Turn Around Time
                path_total = args.inputdir + "/" + policy + "/data-agg/" + wl_show_name + "_tot.csv"
                dft = pd.read_table(path_total, sep=",")

                WTt_value = dft["interval:mean"].iloc[-1]
                WTt_err = dft["interval:std"].iloc[-1]

                dfTotal.ix[numConfig,'Tt:mean'] = WTt_value
                dfTotal.ix[numConfig,'Tt:ci'] = Z * (WTt_err / 3)

                numConfig = numConfig + 1;

            #generate dfTotal table
            outputPathTotal = outputPath + "/totalTables"
            os.makedirs(os.path.abspath(outputPathTotal), exist_ok=True)

            tt = outputPathTotal + "/" + wl_show_name + "-totalDataTable.csv"
            dfTotal = dfTotal.set_index(['policy'])
            #print(tt)
            #print(dfTotal)
            dfTotal.to_csv(tt, sep=',')


# El main es crida des d'ací
if __name__ == "__main__":
    main()

