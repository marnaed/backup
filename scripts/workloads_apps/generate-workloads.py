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
    parser.add_argument('-cr', '--critical', required=True, help='.yaml file where the list of critical apps is found.')
    #parser.add_argument('-md', '--medium', help='.yaml file where the list of medium apps is found.')
    parser.add_argument('-ncr', '--noncritical', help='.yaml file where the list of non critical apps is found.')
    parser.add_argument('-od', '--outputdir', default='./', help='Directory where output file will be placed')

    args = parser.parse_args()


    with open(args.critical, 'r') as f:
        critical = yaml.load(f)
    cr_list = ["-".join(wl) for wl_id, wl in enumerate(critical)]

    with open(args.noncritical, 'r') as f:
        noncritical = yaml.load(f)
    ncr_list = ["-".join(wl) for wl_id, wl in enumerate(noncritical)]

    #with open(args.medium, 'r') as f:
    #    medium = yaml.load(f)
    #md_list = ["-".join(wl) for wl_id, wl in enumerate(medium)]

    fout = args.outputdir + "/workloadsX.yaml"
    with open(fout, 'w') as file:

        for i in range(0,1):
        #for i in range(0,2):
            #choose 1 random cr app and 7 random non-cr apps
            w1 = np.random.choice(ncr_list, 7, replace=False)
            w2 = np.random.choice(cr_list, 1, replace=False)
            w = np.append(w1,w2)

            wprint = "- ["
            for i in w:
                wprint = wprint + i + ", "
            wprint = wprint[:-2]
            wprint = wprint + "]"
            print(wprint)
            file.write(str(wprint) + "\n")

        for i in range(0,3):
        #for i in range(0,2):
            #choose 2 random cr apps and 6 random non-cr apps
            w1 = np.random.choice(ncr_list, 6, replace=False)
            w2 = np.random.choice(cr_list, 2, replace=False)
            w = np.append(w1,w2)

            wprint = "- ["
            for i in w:
                wprint = wprint + i + ", "
            wprint = wprint[:-2]
            wprint = wprint + "]"
            print(wprint)
            file.write(str(wprint) + "\n")


        for i in range(0,3):
        #for i in range(0,2):
            #choose 3 random cr apps and 5 random non-cr apps
            w1 = np.random.choice(ncr_list, 5, replace=False)
            w2 = np.random.choice(cr_list, 3, replace=False)
            w = np.append(w1,w2)

            wprint = "- ["
            for i in w:
                wprint = wprint + i + ", "
            wprint = wprint[:-2]
            wprint = wprint + "]"
            print(wprint)
            file.write(str(wprint) + "\n")


# El main es crida des d'ací
if __name__ == "__main__":
    main()

