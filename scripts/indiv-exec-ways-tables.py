
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

    args = parser.parse_args()

    Z = 1.96  # value of z at a 95% c.i.
    NUM_REPS_APP = 3  # num of values used to calculate the mean value
    SQRT_NUM_REPS_APP = np.sqrt(NUM_REPS_APP)
    NUM_REPS_APP_2 = 6
    SQRT_NUM_REPS_APP_2 = np.sqrt(NUM_REPS_APP_2)
    numApps = 8

    num_experiment = 1

    ## EVENTS INDIVIDUAL (num_experiment = 1) ##
    ev0 = "mem_load_uops_retired.l3_hit"
    ev1 = "mem_load_uops_retired.l3_miss"
    ev2 = "cycle_activity.stalls_ldm_pending"
    ev3 = "intel_cqm/llc_occupancy/"

    ## EVENTS INDIVIDUAL AKC (num_experiment = 2) ##
    #ev0 = "mem_load_uops_retired.l2_miss"
    #ev1 = "mem_load_uops_retired.l3_miss"
    #ev2 = "br_misp_retired.all_branches"
    #ev3 = "br_inst_retired.all_branches"
    #ev4 = "mem_load_uops_retired.l3_hit"

    ## EVENTS INDIVIDUAL PREFETCH (num_experiment = 3) ##
    #ev0 = "l2_rqsts.all_pf"
    #ev1 = "mem_load_uops_retired.l3_miss"
    #ev2 = "cycle_activity.stalls_ldm_pending"
    #ev3 = "intel_cqm/llc_occupancy/"

    ## EVENTS INDIVIDUAL MISSES (num_experiment = 4) ##
    #ev0 = "mem_load_uops_retired.l3_hit"
    #ev1 = "mem_load_uops_retired.l1_miss"
    #ev2 = "mem_load_uops_retired.l2_miss"
    #ev3 = "mem_load_uops_retired.l3_miss"

    ## EVENTS INDIVIDUAL STALLS (num_experiment = 5) ##
    #ev0 = "cycle_activity.stalls_ldm_pending"
    #ev1 = "cycle_activity.stalls_l1d_pending"
    #ev2 = "cycle_activity.stalls_l2_pending"
    #ev3 = "cycle_activity.stalls_mem_any"
    #ev4 = "cycle_activity.stalls_total"

    print(args.workloads)

    with open(args.workloads, 'r') as f:
        workloads = yaml.load(f)

    outputPath= os.path.abspath(args.outputdir)
    os.makedirs(os.path.abspath(outputPath), exist_ok=True)

    for wl_id, wl in enumerate(workloads):

        #name of the file with raw data
        wl_show_name = "-".join(wl)

        # create Data Frame with total values for each workload of the policy
        if num_experiment == 1:
            columns = ['Ways',"IPC:mean","IPC:ci","MPKIL3:mean","MPKIL3:ci","LLCoccup:mean","LLCoccup:ci"]
        elif num_experiment == 2:
            columns = ['Ways',"IPC:mean","IPC:ci","MPKIL3:mean","MPKIL3:ci","AKC:mean","AKC:ci","%correct_branches:mean","%correct_branches:ci","MPKCL3:mean","HitsL3:mean"]
        elif num_experiment == 3:
            columns = ['Ways',"IPC:mean","IPC:ci","MPKIL3:mean","MPKIL3:ci","PfKC:mean","MPKCL3:mean"]
        elif num_experiment == 4:
            columns = ['Ways',"IPC:mean","IPC:ci","MPKIL3:mean","MPKIL3:ci","MPKCL3:mean","%MissesL3:mean"]
        elif num_experiment == 5:
            columns = ['Ways',"IPC:mean","IPC:ci","StallsL1d:mean","StallsL2:mean","StallsL3:mean","StallsLDM:mean","StallsMem:mean","StallsCore:mean","StallsTotal:mean","StallsCoreWrtMem:mean","StallsCoreWrtTotal:mean","StallsMemWrtTotal:mean","StallsCoreWrtCycles:mean","StallsMemWrtCycles:mean","Issue:mean","Cycles:mean","Instructions:mean","Speedup:mean"]

        index = range(0, 20)
        df = pd.DataFrame(columns=columns, index = index)
        df['Ways'] = range(1, 21)

        numW = 0
        for ways in range(1,21):

            wl_name = args.inputdir +  "/" + str(ways) + "w/data-agg/" + wl_show_name + "_fin.csv"
            #print(wl_name)

	    #create dataframe from raw data
            dfWay = pd.read_table(wl_name, sep=",")
            dfWay = dfWay[["cycles:mean","cycles:std","instructions:mean","instructions:std","ipc:mean","ipc:std",ev0+":mean",ev0+":std",ev1+":mean",ev1+":std",ev2+":mean",ev2+":std",ev3+":mean",ev3+":std"]]

            ## IPC ##
            df.ix[numW,'IPC:mean'] = dfWay.ix[0,"ipc:mean"]
            df.ix[numW,'IPC:ci'] = Z * ((np.sqrt((dfWay["ipc:std"]*dfWay["ipc:std"]).sum())) / SQRT_NUM_REPS_APP)

            ## MPKI_LLC ##
            if num_experiment != 5:
                relErrIns = (dfWay["instructions:std"] / dfWay["instructions:mean"] )**2
                relErrEv1 = (dfWay[ev1+":std"] / dfWay[ev1+":mean"] )**2
                relErr = np.sqrt(relErrIns + relErrEv1)
                df.ix[numW,'MPKCL3:mean'] = dfWay.ix[0,ev1+":mean"] / (dfWay.ix[0,"cycles:mean"] / 1000)
                dfWay[ev1+":mean"] = dfWay[ev1+":mean"] / (dfWay["instructions:mean"] / 1000)
                dfWay[ev1+":std"] = dfWay[ev1+":mean"] * relErr
                dfWay[ev1+":std"] = Z * ( dfWay[ev1+":std"] / SQRT_NUM_REPS_APP )
                df.ix[numW,'MPKIL3:mean'] = dfWay.ix[0,ev1+":mean"]
                df.ix[numW,'MPKIL3:ci'] =np.sqrt(dfWay.ix[0,ev1+":std"]*dfWay.ix[0,ev1+":std"])

            if num_experiment == 1:
                dfWay = dfWay[["cycles:mean","cycles:std","instructions:mean","instructions:std","ipc:mean","ipc:std",ev1+":mean",ev1+":std",ev3+":mean",ev3+":std"]]

                ## LLC OCCUPANCY ##
                dfWay[ev3+":mean"] = (dfWay[ev3+":mean"] / 1024) / 1024
                dfWay[ev3+":std"] = (dfWay[ev3+":std"] / 1024) / 1024
                df.ix[numW,'LLCoccup:mean'] = dfWay.ix[0,ev3+":mean"]
                df.ix[numW,'LLCoccup:ci'] = Z * ((np.sqrt((dfWay[ev3+":std"]*dfWay[ev3+":std"]).sum())) / SQRT_NUM_REPS_APP)


            if num_experiment == 2:
                ## ACCESSES PER KILO CYCLE TO THE L3 ##
                dfWay[ev0+":mean"] = dfWay[ev0+":mean"] / (dfWay["cycles:mean"] / 1000)
                df.ix[numW,'AKC:mean'] = dfWay.ix[0,ev0+":mean"]

                ## % OF CORRECT BRANCHES PREDICTED  ##
                dfWay[ev2+":mean"] = dfWay[ev2+":mean"] / dfWay[ev3+":mean"]
                dfWay[ev2+":mean"] = 1 - dfWay[ev2+":mean"]
                dfWay[ev2+":mean"] = dfWay[ev2+":mean"] * 100
                df.ix[numW,'%correct_branches:mean'] = dfWay.ix[0,ev2+":mean"]

                ## NUMBER OF HITS IN THE LLC ##
                name_hits = "/home/lupones/manager/experiments/individual/" + str(ways) + "w/data-agg/" + wl_show_name + "_fin.csv"
                dfHits = pd.read_table(name_hits, sep=",")
                dfHits = dfHits[["mem_load_uops_retired.l3_hit:mean"]]
                df.ix[numW,'HitsL3:mean'] = dfHits.ix[0,ev4+":mean"]

            if num_experiment == 3:
                ## NUMBER OF PREFETCH ACCESSES PER KILO CYCLE ##
                dfWay[ev0+":mean"] = dfWay[ev0+":mean"] / (dfWay["cycles:mean"] / 1000)
                df.ix[numW,'PfKC:mean'] = dfWay.ix[0,ev0+":mean"]

            if num_experiment == 4:
                ## MPKC_LLC ##
                df.ix[numW,'MPKCL3:mean'] = dfWay.ix[0,ev1+":mean"] / (dfWay.ix[0,"cycles:mean"] / 1000)

                ## %MISSES_L3 = l3_misses / (l1_misses + l2_misses + l3_hits) ##
                df.ix[numW,'%MissesL3:mean'] = dfWay.ix[0,ev3+":mean"] / (dfWay.ix[0,ev0+":mean"] + dfWay.ix[0,ev1+":mean"] + dfWay.ix[0,ev2+":mean"])

            if num_experiment == 5:
                # IPC for 20 way cache and 8 apps = 2,5 ways per app
                wl_2w = args.inputdir + "/2w/data-agg/" + wl_show_name + "_fin.csv"
                df2w = pd.read_table(wl_2w, sep=",")
                wl_3w = args.inputdir + "/2w/data-agg/" + wl_show_name + "_fin.csv"
                df3w = pd.read_table(wl_3w, sep=",")
                IPCbase = (df2w.ix[0,"ipc:mean"] + df3w.ix[0,"ipc:mean"]) / 2

                df.ix[numW,'Speedup:mean'] = dfWay.ix[0,"ipc:mean"] / IPCbase

                df.ix[numW,'Cycles:mean'] = dfWay.ix[0,"cycles:mean"]
                df.ix[numW,'Instructions:mean'] = dfWay.ix[0,"instructions:mean"]

                df.ix[numW,'StallsLDM:mean'] = dfWay.ix[0,ev0+":mean"] / 10000000000
                df.ix[numW,'StallsL1d:mean'] = dfWay.ix[0,ev1+":mean"] / 10000000000
                df.ix[numW,'StallsL2:mean'] = dfWay.ix[0,ev2+":mean"] / 10000000000
                df.ix[numW,'StallsL3:mean'] = dfWay.ix[0,ev3+":mean"] - dfWay.ix[0,ev2+":mean"]
                df.ix[numW,'StallsL3:mean'] = df.ix[numW,'StallsL3:mean'] / 10000000000
                df.ix[numW,'StallsCore:mean'] = dfWay.ix[0,ev4+":mean"] - dfWay.ix[0,ev3+":mean"]
                df.ix[numW,'StallsCoreWrtCycles:mean'] = df.ix[numW,'StallsCore:mean'] / dfWay.ix[0,"cycles:mean"]
                df.ix[numW,'StallsCoreWrtCycles:mean'] = df.ix[numW,'StallsCoreWrtCycles:mean'] * 100
                df.ix[numW,'StallsCore:mean'] = df.ix[numW,'StallsCore:mean'] / 10000000000
                df.ix[numW,'StallsMem:mean'] = dfWay.ix[0,ev3+":mean"] / 10000000000
                df.ix[numW,'StallsTotal:mean'] = dfWay.ix[0,ev4+":mean"] / 10000000000
                df.ix[numW,'StallsCoreWrtMem:mean'] = df.ix[numW,'StallsCore:mean'] / df.ix[numW,'StallsMem:mean']
                df.ix[numW,'StallsCoreWrtMem:mean'] = df.ix[numW,'StallsCoreWrtMem:mean'] * 100
                df.ix[numW,'StallsCoreWrtTotal:mean'] = df.ix[numW,'StallsCore:mean'] / df.ix[numW,'StallsTotal:mean']
                df.ix[numW,'StallsCoreWrtTotal:mean'] = df.ix[numW,'StallsCoreWrtTotal:mean'] * 100
                df.ix[numW,'StallsMemWrtTotal:mean'] = df.ix[numW,'StallsMem:mean'] / df.ix[numW,'StallsTotal:mean']
                df.ix[numW,'StallsMemWrtTotal:mean'] = df.ix[numW,'StallsMemWrtTotal:mean'] * 100
                df.ix[numW,'StallsMemWrtCycles:mean'] =  dfWay.ix[0,ev3+":mean"] / dfWay.ix[0,"cycles:mean"]
                df.ix[numW,'StallsMemWrtCycles:mean'] = df.ix[numW,'StallsMemWrtCycles:mean'] * 100

                df.ix[numW,'Issue:mean'] = dfWay.ix[0,"ipc:mean"] + (dfWay.ix[0,ev4+":mean"] / dfWay.ix[0,"cycles:mean"])

            numW = int(numW) + 1

        # save table
        df = df.set_index('Ways')
        print(wl_show_name)
        outputPathPolicy = outputPath + "/" + wl_show_name + "_num-ways-table.csv"
        df.to_csv(outputPathPolicy, sep=',')



# El main es crida des d'ací
if __name__ == "__main__":
    main()

