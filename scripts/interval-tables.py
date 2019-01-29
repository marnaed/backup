import argparse
import numpy as np
import os
import pandas as pd
import re
import scipy.stats
import sys
import yaml
import glob
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def main():
    parser = argparse.ArgumentParser(description='Process results of workloads by intervals.')
    parser.add_argument('-w', '--workloads', required=True, help='.yaml file where the list of workloads is found.')
    parser.add_argument('-od', '--outputdir', default='./output', help='Directory where output files will be placed')
    parser.add_argument('-id', '--inputdir', default='./data', help='Directory where input are found')
    parser.add_argument('-p', '--policies', action='append', default=[], help='Policies we want to show results of. Options: noPart,criticalAware.')
    args = parser.parse_args()
    Z = 1.96  # value of z at a 95% c.i.
    NUM_REPS_APP = 3  # num of values used to calculate the mean value
    SQRT_NUM_REPS_APP = np.sqrt(NUM_REPS_APP)
    NUM_REPS_APP_2 = 6
    SQRT_NUM_REPS_APP_2 = np.sqrt(NUM_REPS_APP_2)
    numApps = 8

    ## EVENTS USED ##
    ev0 = "mem_load_uops_retired.l2_miss"
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
                ##dfWay = dfWay[["interval","app","instructions:mean","instructions:std","cycles:mean","cycles:std","ipc:mean","ipc:std",ev1+":mean",ev1+":std",ev2+":mean",ev2+":std",ev3+":mean",ev3+":std"]]
                dfWay = dfWay[["interval","app","instructions:mean","instructions:std","cycles:mean","cycles:std","ipc:mean","ipc:std",ev1+":mean",ev1+":std",ev2+":mean",ev2+":std",ev3+":mean",ev3+":std"]]

                # Calculate confidence interval of Memory Stalls
                ##dfWay[ev2+":std"] =  Z * ( dfWay[ev2+":std"] / SQRT_NUM_REPS_APP )

                # Calculate confidence interval of Mem IPC
                dfWay[ev2+":std"] =  Z * ( dfWay[ev2+":std"] / SQRT_NUM_REPS_APP )
                dfWay["ipc:std"] =  Z * ( dfWay["ipc:std"] / SQRT_NUM_REPS_APP )

                # Calculate LLC occupancy
                dfWay[ev3+":mean"] = (dfWay[ev3+":mean"] / 1024) / 1024
                dfWay[ev3+":std"] = (dfWay[ev3+":std"] / 1024) / 1024
                dfWay[ev3+":std"] = Z * ( dfWay[ev3+":std"] / SQRT_NUM_REPS_APP )

                # Rename columns
                ##dfWay = dfWay.rename(columns={'ipc:mean': 'IPC:mean', 'ipc:std': 'IPC:ci', ev3+':mean': 'l3_Mbytes_occ:mean', ev3+':std': 'l3_Mbytes_occ:ci', ev2+':mean': 'Mem_Stalls:mean', ev2+':std': 'Mem_Stalls:ci'})
                dfWay = dfWay.rename(columns={'ipc:mean': 'IPC:mean', 'ipc:std': 'IPC:ci', ev3+':mean': 'l3_Mbytes_occ:mean', ev3+':std': 'l3_Mbytes_occ:ci', ev2+":mean": "Stalls_ldm:mean", ev2+":std": "Stalls_ldm:ci"})

                # Calculate MPKI_L3
                relErrIns = (dfWay["instructions:std"] / dfWay["instructions:mean"] )**2
                relErrEv1 = (dfWay[ev1+":std"] / dfWay[ev1+":mean"] )**2
                relErr = np.sqrt(relErrIns + relErrEv1)
                dfWay[ev1+":mean"] = dfWay[ev1+":mean"] / (dfWay["instructions:mean"] / 1000)
                dfWay[ev1+":std"] = dfWay[ev1+":mean"] * relErr
                dfWay[ev1+":std"] = Z * ( dfWay[ev1+":std"] / SQRT_NUM_REPS_APP )
                dfWay = dfWay.rename(columns={ev1+':mean': 'MPKIL3:mean', ev1+':std': 'MPKIL3:ci'})

                # Calculate MPKI_L2
                #relErrIns = (dfWay["instructions:std"] / dfWay["instructions:mean"] )**2
                #relErrEv0 = (dfWay[ev0+":std"] / dfWay[ev0+":mean"] )**2
                #relErr = np.sqrt(relErrIns + relErrEv0)
                #dfWay[ev0+":mean"] = dfWay[ev0+":mean"] / (dfWay["instructions:mean"] / 1000)
                #dfWay[ev0+":std"] = dfWay[ev0+":mean"] * relErr
                #dfWay[ev0+":std"] = Z * ( dfWay[ev0+":std"] / SQRT_NUM_REPS_APP )
                #dfWay = dfWay.rename(columns={ev0+':mean': 'MPKIL2:mean', ev0+':std': 'MPKIL2:ci'})

                # Interval tables app per workload
                ##dfWay = dfWay[["interval","app","IPC:mean","IPC:ci","l3_Mbytes_occ:mean","l3_Mbytes_occ:ci","MPKIL3:mean","MPKIL3:ci","Mem_Stalls:mean","Mem_Stalls:ci"]]
                dfWay = dfWay[["interval","app","IPC:mean","IPC:ci","MPKIL3:mean","MPKIL3:ci","l3_Mbytes_occ:mean","l3_Mbytes_occ:ci","Stalls_ldm:mean","Stalls_ldm:ci"]]

                dfWayAux = dfWay
                dfWay = dfWay.set_index(['interval','app'])
                groups = dfWay.groupby(level=[1])

                # create DF for LLC of all apps in workload
                columns = ['interval']
                #print(numApps)
                #print(dfWay)
                ##numRows = dfWay['IPC:mean'].count()/numApps
                ##numRows = np.asscalar(np.int16(numRows))
                ##index = range(0, numRows)
                ##dfLLC = pd.DataFrame(columns=columns, index = index)
                ##dfLLC["interval"] = range(0, numRows)

                ##LLCDATAF = dfLLC

                ##totalmeanLLC = 0;


                for appName, df in groups:
                    # Generate interval_data_table for each app
                    outputPathWaysWorkload = outputPathPolicy + "/" + wl_show_name
                    os.makedirs(os.path.abspath(outputPathWaysWorkload), exist_ok=True)
                    outputPathApp = outputPathWaysWorkload + "/" + appName + "-intervalDataTable.csv"
                    df.to_csv(outputPathApp, sep=',')

                    # Plot in scatter plot
                    #Y = df["MPKIL3:mean"].tolist()
                    X = df.index.get_level_values('interval')
                    #Y = df["l3_Mbytes_occ:mean"].tolist()
                    Y = df["Stalls_ldm:mean"].tolist()
                    plt.scatter(X,Y,marker="o",label=appName)



                    #add column to LLC_occup dataframe
                    #nameC = "LLC" + app
                    #print(list(dfWayAux.columns.values))
                    #print(numRows)
                    ##AuxAPP = dfWayAux[dfWayAux.app.isin([appName])]
                    ##AuxAPP = AuxAPP['l3_Mbytes_occ:mean']
                    ##AuxAPP.index = range(0, numRows)
                    #print(AuxAPP)
                    ##LLCDATAF[appName] = AuxAPP
                    ##totalmeanLLC = totalmeanLLC + AuxAPP.mean()

                outputPathApp = outputPathPolicy + "/INT-STALLS-" + wl_show_name + "-scatter.pdf"
                plt.legend(loc=0)
                #plt.xlabel('MPKIL3')
                plt.xlabel('Interval')
                #plt.ylim(0,20)
                #plt.ylabel('MPKIL3')
                plt.ylabel('Memory stalls')
                plt.show()
                plt.savefig(outputPathApp)

                plt.clf()
                plt.cla()
                plt.close()

                # save LLC_occup dataframe
                #print(LLCDATAF)
                ##print(policy + ": " + wl_show_name + " has mean LLC occup: " + str(totalmeanLLC))
                ##LLCDATAF = LLCDATAF.set_index(['interval'])
                ##outputPathLLC = outputPathWaysWorkload + "/LLC_occup_apps_data_table.csv"
                ##LLCDATAF.to_csv(outputPathLLC, sep=',')



# El main es crida des d'ací
if __name__ == "__main__":
    main()

