#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <memory>
#include <tuple>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <fmt/format.h>

#include "cat-linux-policy.hpp"
#include "kmeans.hpp"
#include "log.hpp"


namespace cat
{
namespace policy
{


namespace acc = boost::accumulators;
using std::string;
using fmt::literals::operator""_format;

// varaible to assign tasks or cores to CLOS: task / cpu
const std::string CLOS_ADD = "task";

// No Part Policy
void NoPart::apply(uint64_t current_interval, const tasklist_t &tasklist)
{
    // Apply only when the amount of intervals specified has passed
    if (current_interval % every != 0)
        return;

    double ipcTotal = 0;

    LOGINF("CAT Policy name: NoPart");
    LOGINF("Using {} stats"_format(stats));

	for (const auto &task_ptr : tasklist)
	{
		const Task &task = *task_ptr;
		std::string taskName = task.name;

		double inst = 0, cycl = 0, ipc = -1;

        assert((stats == "total") | (stats == "interval"));

        if (stats == "total")
        {
            // Cycles and IPnC
            inst = task.stats.sum("instructions");
			cycl = task.stats.sum("cycles");

        }
        else if (stats == "interval")
        {
            // Cycles and IPnC
            inst = task.stats.last("instructions");
			cycl = task.stats.last("cycles");
        }

		ipc = inst / cycl;
        ipcTotal += ipc;
	}
}


// Auxiliar method of Critical Alone policy to reset configuration
void CriticalAware::reset_configuration(const tasklist_t &tasklist)
{
    //assign all tasks to CLOS 1
	if(CLOS_ADD == "task")
	{
		for (const auto &task_ptr : tasklist)
    	{
			const Task &task = *task_ptr;
        	pid_t taskPID = task.pid;
			LinuxBase::get_cat()->add_task(1,taskPID);
		}
	}
	else
	{
		//assign all cores to CLOS 1
		for (uint32_t c = 0; c < 8; c++)
    	{
        	LinuxBase::get_cat()->add_cpu(1,c);
    	}
	}

    //change masks of CLOS to 0xfffff
    LinuxBase::get_cat()->set_cbm(1,0xfffff);
    LinuxBase::get_cat()->set_cbm(2,0xfffff);

    firstTime = 1;
    state = 0;
    expectedIPCtotal = 0;

    maskCrCLOS = 0xfffff;
    maskNonCrCLOS = 0xfffff;

    num_ways_CLOS_2 = 20;
    num_ways_CLOS_1 = 20;

    num_shared_ways = 0;

    idle = false;
    idle_count = IDLE_INTERVALS;

    LOGINF("Reset performed. Original configuration restored");
}

double CriticalAware::medianV(std::vector<pairD_t> &vec)
{
	double med;
    size_t size = vec.size();

    if (size  % 2 == 0)
    {
    	med = (std::get<1>(vec[size/2 - 1]) + std::get<1>(vec[size/2])) / 2;
    }
    else
    {
    	med = std::get<1>(vec[size/2]);
    }
	return med;
}

void CriticalAware::apply(uint64_t current_interval, const tasklist_t &tasklist)
{
    LOGINF("Current_interval = {}"_format(current_interval));
    // Apply only when the amount of intervals specified has passed
    if (current_interval % every != 0)
        return;

    // (Core, MPKI-L3) tuple
	auto v = std::vector<pairD_t>();
    auto v_ipc = std::vector<pairD_t>();
	auto v_l3_occup_mb = std::vector<pairD_t>();

	// Vector with current active tasks
    auto active_tasks = std::vector<pid_t>();

	auto outlier = std::vector<pair_t>();

    double ipcTotal = 0, mpkiL3Total = 0;
    //double missesL3Total = 0, instsTotal = 0;
	double ipc_CR = 0;
    double ipc_NCR = 0;
	double l3_occup_mb_total = 0;

    uint64_t newMaskNonCr, newMaskCr;

    // Number of critical apps found in the interval
    uint32_t critical_apps = 0;
    bool change_in_outliers = false;

    LOGINF("CAT Policy name: Critical-Aware");

	// Gather data
	for (const auto &task_ptr : tasklist)
    {
    	const Task &task = *task_ptr;
        std::string taskName = task.name;
		pid_t taskPID = task.pid;
		uint32_t cpu = task.cpus.front();

		// stats per interval
		uint64_t l3_miss = task.stats.last("mem_load_uops_retired.l3_miss");
		uint64_t inst = task.stats.last("instructions");
		double ipc = task.stats.last("ipc");
		double l3_occup_mb = task.stats.last("intel_cqm/llc_occupancy/") / 1024 / 1024;

		l3_occup_mb_total += l3_occup_mb;

		double MPKIL3 = (double)(l3_miss*1000) / (double)inst;

        //LOGINF("Task {}: MPKI_L3 = {}"_format(taskName,MPKIL3));
        LOGINF("Task {} ({}): IPC = {}, MPKI_L3 = {}, l3_occup_mb {}"_format(taskName,taskPID,ipc,MPKIL3,l3_occup_mb));
		v.push_back(std::make_pair(taskPID, MPKIL3));
        v_ipc.push_back(std::make_pair(taskPID, ipc));
		pid_CPU.push_back(std::make_pair(taskPID,cpu));
		active_tasks.push_back(taskPID);

        ipcTotal += ipc;
		mpkiL3Total += MPKIL3;
	}

	// Perform no further action if cache-warmup time has not passed
    if (current_interval < firstInterval)
		return;

    // Check if taskIsInCRCLOS holds only current tasks
    // aux = vector to store reset values
    auto aux = std::vector<pair_t>();
    for (const auto &item : taskIsInCRCLOS)
    {
    	pid_t taskPID = std::get<0>(item);
        uint64_t CLOS_val =std::get<1>(item);
        if ( std::find(active_tasks.begin(), active_tasks.end(), taskPID) == active_tasks.end() )
        {
        	LOGINF("TASK {} HAS BEEN RESTARTED "_format(taskPID));
            // Save task no longer active
            aux.push_back(std::make_pair(taskPID,CLOS_val));
        }
    }

    if (!aux.empty())
    {
		// Remove tasks no longer active from taskIsInCRCLOS
        for (const auto &item : aux)
        {
        	pid_t taskPID = std::get<0>(item);
            auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&taskPID](const auto& tuple) {return std::get<0>(tuple)  == taskPID;});
            it2 = taskIsInCRCLOS.erase(it2);
        }
        aux.clear();

		// Add new active tasks to taskIsInCRCLOS
        for (const auto &item : active_tasks)
        {
            pid_t taskPID = item;
            auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&taskPID](const auto& tuple) {return std::get<0>(tuple)  == taskPID;});
            if(it2 == taskIsInCRCLOS.end())
            {
                // Check CLOS value of task
                uint64_t CLOS_val = LinuxBase::get_cat()->get_clos_of_task(taskPID);

                // Add new pair
                taskIsInCRCLOS.push_back(std::make_pair(taskPID,CLOS_val));
                LOGINF("RESTARTED TASK {} in CLOS {} HAS BEEN ADDED to taskIsInCRCLOS"_format(taskPID,CLOS_val));
            }
        }
    }

    // calculate total MPKIL3 mean of interval
	double meanMPKIL3Total = mpkiL3Total / tasklist.size();
	LOGINF("Total L3 occupation: {}"_format(l3_occup_mb_total));
	LOGINF("Mean MPKI_LLC_Total (/{}) = {}"_format(tasklist.size(), meanMPKIL3Total));

    // PROCESS DATA
    if (current_interval >= firstInterval)
    {
		// MAD = Median Absolute Value
 		// 1. Sort in ascending order vector v
 		//std::sort(v.begin(), v.end(), [](const std::tuple<pid_t, double> &left, const std::tuple<pid_t, double> &right) {
    	//		return std::get<1>(left) < std::get<1>(right);
 		//});

 		// 2. Find the median
 		//double Mj = medianV(v);

 		// 3. Subtract from each value the median
 		//auto v_sub = v;
 		//for (std::tuple<pid_t, double> &tup : v_sub)
 		//{
     	//	std::get<1>(tup) = fabs (std::get<1>(tup) - Mj);
 		//}

		// 4. Sort in ascending order the new set of values
		//std::sort(v_sub.begin(), v_sub.end(), [](const std::tuple<pid_t, double> &left, const std::tuple<pid_t, double> &right) {
		//	   return std::get<1>(left) < std::get<1>(right);
 		//});

        // 5. Find the median
 		//double Mi = medianV(v_sub);

		// 6. Multiply median by b (assume normal distribution)
		//double MAD = Mi * 1.4826;

		// 7. Calculate limit_outlier
		//double limit_outlier = Mj + 3*MAD;

		// MEAN AND STD LIMIT OUTLIER CALCULATION
		//accumulate value
		macc(meanMPKIL3Total);

		//calculate rolling mean
		mpkiL3Mean = acc::rolling_mean(macc);
     	LOGINF("Rolling mean of MPKI-L3 at interval {} = {}"_format(current_interval, mpkiL3Mean));

		//calculate rolling std and limit of outlier
		stdmpkiL3Mean = std::sqrt(acc::rolling_variance(macc));
		LOGINF("stdMPKILLCmean = {}"_format(stdmpkiL3Mean));

		//calculate limit outlier
		double limit_outlier = mpkiL3Mean + 3*stdmpkiL3Mean;
		LOGINF("limit_outlier = {}"_format(limit_outlier));


		pid_t pidTask;
		//Check if MPKI-L3 of each APP is 2 stds o more higher than the mean MPKI-L3
        for (const auto &item : v)
        {
            double MPKIL3Task = std::get<1>(item);
            pidTask = std::get<0>(item);
			int freqCritical = -1;
			double fractionCritical = 0;

			if(current_interval > firstInterval)
			{
				//Search for mi tuple and update the value
				auto it = frequencyCritical.find(pidTask);
				if(it != frequencyCritical.end())
				{
					freqCritical = it->second;
				}
				else
				{
					LOGINF("TASK RESTARTED --> INCLUDE IT AGAIN IN frequencyCritical");
					frequencyCritical[pidTask] = 0;
					freqCritical = 0;
				}
				assert(freqCritical>=0);
				fractionCritical = freqCritical / (double)(current_interval-firstInterval);
				LOGINF("Fraction Critical ({} / {}) = {}"_format(freqCritical,(current_interval-firstInterval),fractionCritical));
			}


            if (MPKIL3Task >= limit_outlier)
            {
                LOGINF("The MPKI_LLC of task with pid {} is an outlier, since {} >= {}"_format(pidTask,MPKIL3Task,limit_outlier));
                outlier.push_back(std::make_pair(pidTask,1));
                critical_apps = critical_apps + 1;

				// increment frequency critical
				frequencyCritical[pidTask]++;
            }
            else if(MPKIL3Task < limit_outlier && fractionCritical>=0.5)
			{
				LOGINF("The MPKI_LLC of task with pid {} is NOT an outlier, since {} < {}"_format(pidTask,MPKIL3Task,limit_outlier));
				LOGINF("Fraction critical of {} is {} --> CRITICAL"_format(pidTask,fractionCritical));

				outlier.push_back(std::make_pair(pidTask,1));
                critical_apps = critical_apps + 1;
			}
			else
            {
				// it's not a critical app
				LOGINF("The MPKI_LLC of task with pid {} is NOT an outlier, since {} < {}"_format(pidTask,MPKIL3Task,limit_outlier));
                outlier.push_back(std::make_pair(pidTask,0));

				// initialize counter if it's the first interval
				if(current_interval == firstInterval)
				{
					frequencyCritical[pidTask] = 0;
				}
            }
        }

		LOGINF("critical_apps = {}"_format(critical_apps));

        //check CLOS are configured to the correct mask
        if (firstTime)
        {
            // set ways of CLOS 1 and 2
            switch ( critical_apps )
            {
                case 1:
                    // 1 critical app = 12cr10others
                    maskCrCLOS = 0xfff00;
                    num_ways_CLOS_2 = 12;
                    maskNonCrCLOS = 0x003ff;
                    num_ways_CLOS_1 = 10;
                    state = 1;
                    break;
                case 2:
                    // 2 critical apps = 13cr9others
                    maskCrCLOS = 0xfff80;
                    num_ways_CLOS_2 = 13;
                    maskNonCrCLOS = 0x001ff;
                    num_ways_CLOS_1 = 9;
                    state = 2;
                    break;
                case 3:
                    // 3 critical apps = 14cr8others
                    maskCrCLOS = 0xfffc0;
                    num_ways_CLOS_2 = 14;
                    maskNonCrCLOS = 0x000ff;
                    num_ways_CLOS_1 = 8;
                    state = 3;
                    break;
                default:
                     // no critical apps or more than 3 = 20cr20others
                     maskCrCLOS = 0xfffff;
                     num_ways_CLOS_2 = 20;
                     maskNonCrCLOS = 0xfffff;
                     num_ways_CLOS_1 = 20;
                     state = 4;
                     break;
            } // close switch

            num_shared_ways = 2;
            LinuxBase::get_cat()->set_cbm(1,maskNonCrCLOS);
            LinuxBase::get_cat()->set_cbm(2,maskCrCLOS);

            LOGINF("COS 2 (CR) now has mask {:#x}"_format(maskCrCLOS));
            LOGINF("COS 1 (non-CR) now has mask {:#x}"_format(maskNonCrCLOS));


            firstTime = 0;
			//assign each core to its corresponding CLOS
            for (const auto &item : outlier)
            {
            	pidTask = std::get<0>(item);
                uint32_t outlierValue = std::get<1>(item);

				auto it = std::find_if(v_ipc.begin(), v_ipc.end(),[&pidTask](const auto& tuple) {return std::get<0>(tuple) == pidTask;});
				double ipcTask = std::get<1>(*it);

				double cpuTask;
				if(CLOS_ADD == "cpu")
				{
					auto it1 = std::find_if(pid_CPU.begin(), pid_CPU.end(),[&pidTask](const auto& tuple) {return std::get<0>(tuple) == pidTask;});
                   	cpuTask = std::get<1>(*it1);
				}

                if(outlierValue)
                {
					if(CLOS_ADD == "cpu")
					{
						LinuxBase::get_cat()->add_cpu(2,cpuTask);
						LOGINF("Task in cpu {} assigned to CLOS 2"_format(cpuTask));
					}
					else
					{
                    	LinuxBase::get_cat()->add_task(2,pidTask);
						LOGINF("Task PID {} assigned to CLOS 2"_format(pidTask));
					}
                    taskIsInCRCLOS.push_back(std::make_pair(pidTask,2));

                    ipc_CR += ipcTask;
                }
                else
                {
					if(CLOS_ADD == "cpu")
					{
						LinuxBase::get_cat()->add_cpu(1,cpuTask);
						LOGINF("Task in cpu {} assigned to CLOS 1"_format(cpuTask));
					}
					else
					{
						LinuxBase::get_cat()->add_task(1,pidTask);
						LOGINF("Task PID {} assigned to CLOS 1"_format(pidTask));
	                }

                    taskIsInCRCLOS.push_back(std::make_pair(pidTask,1));
                    ipc_NCR += ipcTask;
                }
            }
	}
	else
	{
		//check if there is a new critical app
            for (const auto &item : outlier)
            {

				pidTask = std::get<0>(item);
            	uint32_t outlierValue = std::get<1>(item);

				auto it = std::find_if(v_ipc.begin(), v_ipc.end(),[&pidTask](const auto& tuple) {return std::get<0>(tuple) == pidTask;});
				double ipcTask = std::get<1>(*it);

				auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&pidTask](const auto& tuple) {return std::get<0>(tuple) == pidTask;});
				uint64_t CLOSvalue = std::get<1>(*it2);
				assert((CLOSvalue == 1) | (CLOSvalue == 2));


                if(outlierValue && (CLOSvalue % 2 != 0))
                {
                    LOGINF("There is a new critical app (outlier {}, current CLOS {})"_format(outlierValue,CLOSvalue));
                    change_in_outliers = true;
                }
                else if(!outlierValue && (CLOSvalue == 2))
                {
                    LOGINF("There is a critical app that is no longer critical)");
                    change_in_outliers = true;
                }
                else if(outlierValue)
                {
                    ipc_CR += ipcTask;
                }
                else
                {
                    ipc_NCR += ipcTask;
                }
            }

			//reset configuration if there is a change in critical apps
            if(change_in_outliers)
            {
                taskIsInCRCLOS.clear();
                reset_configuration(tasklist);

            }
			else if(idle)
            {
                LOGINF("Idle interval {}"_format(idle_count));
                idle_count = idle_count - 1;
                if(idle_count == 0)
                {
                    idle = false;
                    idle_count = IDLE_INTERVALS;
                }

            }
			else if(!idle)
			{
                // if there is no new critical app, modify mask if not done previously
                if(critical_apps>0 && critical_apps<4)
                {
                    LOGINF("IPC total = {}"_format(ipcTotal));
                    LOGINF("Expected IPC total = {}"_format(expectedIPCtotal));

                    double UP_limit_IPC = expectedIPCtotal * 1.04;
                    double LOW_limit_IPC = expectedIPCtotal  * 0.96;
                    double NCR_limit_IPC = ipc_NCR_prev*0.96;
                    double CR_limit_IPC = ipc_CR_prev*0.96;


					if(ipcTotal > UP_limit_IPC)
						LOGINF("New IPC is BETTER: IPCtotal {} > {}"_format(ipcTotal,UP_limit_IPC));
					else if((ipc_CR < CR_limit_IPC) && (ipc_NCR >= NCR_limit_IPC))
						LOGINF("WORSE CR IPC: CR {} < {} && NCR {} >= {}"_format(ipc_CR,CR_limit_IPC,ipc_NCR,NCR_limit_IPC));
					else if((ipc_NCR < NCR_limit_IPC) && (ipc_CR >= CR_limit_IPC))
						LOGINF("WORSE NCR IPC: NCR {} < {} && CR {} >= {}"_format(ipc_NCR,NCR_limit_IPC,ipc_CR,CR_limit_IPC));
					else if( (ipc_CR < CR_limit_IPC) && (ipc_NCR < NCR_limit_IPC))
						LOGINF("BOTH IPCs are WORSE: CR {} < {} && NCR {} < {}"_format(ipc_CR,CR_limit_IPC,ipc_NCR,NCR_limit_IPC));
					else
						LOGINF("BOTH IPCs are EQUAL (NOT WORSE)");

					//transitions switch-case
					switch (state)
					{
						case 1: case 2: case 3:
							if(ipcTotal > UP_limit_IPC)
								idle = true;
							else if((ipcTotal <= UP_limit_IPC) && (ipcTotal >= LOW_limit_IPC))
								state = 5;
							else if((ipc_NCR < NCR_limit_IPC) && (ipc_CR >= CR_limit_IPC))
								state = 6;
							else if((ipc_CR < CR_limit_IPC) && (ipc_NCR >= NCR_limit_IPC))
								state = 5;
							else
								state = 5;
							break;

						case 5: case 6:
							if(ipcTotal > UP_limit_IPC)
								idle = true;
							else if( (ipcTotal <= UP_limit_IPC) && (ipcTotal >= LOW_limit_IPC))
								state = 8;
							else if((ipc_NCR < NCR_limit_IPC) && (ipc_CR >= CR_limit_IPC))
								state = 7;
							else if((ipc_CR < CR_limit_IPC) && (ipc_NCR >= NCR_limit_IPC))
								state = 8;
							else // NCR and CR worse
								state = 8;
							break;

						case 7: case 8:
							if(ipcTotal > UP_limit_IPC)
								idle = true;
							else if((ipcTotal <= UP_limit_IPC) && (ipcTotal >= LOW_limit_IPC))
								state = 5;
							else if((ipc_NCR < NCR_limit_IPC) && (ipc_CR >= CR_limit_IPC))
								state = 6;
							else if((ipc_CR < CR_limit_IPC) && (ipc_NCR >= NCR_limit_IPC))
								state = 5;
							else // NCR and CR worse
								state = 5;
							break;
					}

					// State actions switch-case
					switch ( state )
					{
						case 1: case 2: case 3:
							if(idle)
								LOGINF("New IPC is better or equal-> {} idle intervals"_format(IDLE_INTERVALS));
							else
								LOGINF("No action performed");
							break;

						case 5:
							if(idle)
								LOGINF("New IPC is better or equal -> {} idle intervals"_format(IDLE_INTERVALS));
							else
							{
								LOGINF("NCR-- (Remove one shared way from CLOS with non-critical apps)");
								newMaskNonCr = (maskNonCrCLOS >> 1) | 0x00010;
								maskNonCrCLOS = newMaskNonCr;
								LinuxBase::get_cat()->set_cbm(1,maskNonCrCLOS);
							}
							break;

						case 6:
							if(idle)
								LOGINF("New IPC is better or equal -> {} idle intervals"_format(IDLE_INTERVALS));
							else
							{
								LOGINF("CR-- (Remove one shared way from CLOS with critical apps)");
								newMaskCr = (maskCrCLOS << 1) & 0xfffff;
								maskCrCLOS = newMaskCr;
								LinuxBase::get_cat()->set_cbm(2,maskCrCLOS);
							}
							break;

						case 7:
							if(idle)
								LOGINF("New IPC is better or equal -> {} idle intervals"_format(IDLE_INTERVALS));
							else
							{
								LOGINF("NCR++ (Add one shared way to CLOS with non-critical apps)");
								newMaskNonCr = (maskNonCrCLOS << 1) | 0x00010;
								maskNonCrCLOS = newMaskNonCr;
								LinuxBase::get_cat()->set_cbm(1,maskNonCrCLOS);
							}
							break;

						case 8:
							if(idle)
								LOGINF("New IPC is better or equal -> {} idle intervals"_format(IDLE_INTERVALS));
							else
							{
								LOGINF("CR++ (Add one shared way to CLOS with critical apps)");
								newMaskCr = (maskCrCLOS >> 1) | 0x80000;
								maskCrCLOS = newMaskCr;
								LinuxBase::get_cat()->set_cbm(2,maskCrCLOS);
							}
							break;
						default:
							break;

					}

                    num_ways_CLOS_1 = __builtin_popcount(LinuxBase::get_cat()->get_cbm(1));
                    num_ways_CLOS_2 = __builtin_popcount(LinuxBase::get_cat()->get_cbm(2));

					LOGINF("COS 2 (CR)     has mask {:#x} ({} ways)"_format(LinuxBase::get_cat()->get_cbm(2),num_ways_CLOS_2));
                    LOGINF("COS 1 (non-CR) has mask {:#x} ({} ways)"_format(LinuxBase::get_cat()->get_cbm(1),num_ways_CLOS_1));

					int64_t aux_ns = (num_ways_CLOS_2 + num_ways_CLOS_1) - 20;
                    num_shared_ways = (aux_ns < 0) ? 0 : aux_ns;
                    LOGINF("Number of shared ways: {}"_format(num_shared_ways));
                    assert(num_shared_ways >= 0);

                } //if(critical>0 && critical<4)

            } //else if(!idle)
        }//else no es firstime
        LOGINF("Current state = {}"_format(state));
    }//if (current_interval >= firstInterval)

    //calculate new gradient

    ipc_CR_prev = ipc_CR;
    ipc_NCR_prev = ipc_NCR;

	// Assign total IPC of this interval to previos value
    expectedIPCtotal = ipcTotal;


}//apply


/////////////// CRITICAL-AWARE v2 ///////////////

void CriticalAwareV2::reset_configuration(const tasklist_t &tasklist)
{
    for (const auto &task_ptr : tasklist)
    {
        const Task &task = *task_ptr;
        pid_t taskPID = task.pid;
        LinuxBase::get_cat()->add_task(1,taskPID);
    }

	taskIsInCRCLOS.clear();

    //change masks of CLOS to 0xfffff
    LinuxBase::get_cat()->set_cbm(1,0xfffff);
    LinuxBase::get_cat()->set_cbm(2,0xfffff);

    firstTime = 1;
    state = 0;
    expectedIPCtotal = 0;

    maskCrCLOS = 0xfffff;
    maskNonCrCLOS = 0xfffff;

    num_ways_CLOS_2 = 20;
    num_ways_CLOS_1 = 20;

    num_shared_ways = 0;

    idle = false;
    idle_count = IDLE_INTERVALS;

    LOGINF("Reset performed. Original configuration restored");
}


/*
 * Auxiliar method of Critical Alone policy to reset configuration
 */
void CriticalAwareV2::update_configuration(std::vector<pair_t> v, std::vector<pair_t> status, uint64_t num_critical_old, uint64_t num_critical_new)
{

	uint64_t new_clos;

	// 1. Update global variables
	if((num_critical_new == 0) | (num_critical_new > 4))
		state = 4;
	else
		state = num_critical_new;
	CLOS_key = 3;
	idle = false;
	idle_count = IDLE_INTERVALS;

	LOGINF("YYY From {} ways to {} ways"_format(num_critical_old,num_critical_new));

	// If 4 critical apps detected
	if((num_critical_new == 0) | (num_critical_new >= 4)){
		// assign CLOSes mask 0xfffff
		for( int clos = 1; clos <= 3; clos += 1 )
		{
			//if (clos != 4)
			LinuxBase::get_cat()->set_cbm(clos,0xfffff);
		}
		// assign all apps to CLOS 1
		for (const auto &item : v)
		{
			uint32_t taskID = std::get<0>(item);
			uint64_t CLOS = std::get<1>(item);
			if ((CLOS != 1) && (CLOS != 4))
			{
				LinuxBase::get_cat()->add_task(1,taskID);
				auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&taskID](const auto& tuple) {return std::get<0>(tuple)  == taskID;});
				it2 = taskIsInCRCLOS.erase(it2);
				taskIsInCRCLOS.push_back(std::make_pair(taskID,1));
			}
		}
		LOGINF("All tasks assigned to CLOS 1. TaskIsInCRCLOS updated");
		return;
	}

	// 2. Assign apps to new CLOSes
	for (const auto &item : v)
    {
		uint32_t taskID = std::get<0>(item);
		auto it = std::find_if(status.begin(), status.end(),[&taskID](const auto& tuple) {return std::get<0>(tuple)  == taskID;});
		auto it1 = std::find_if(id_pid.begin(), id_pid.end(),[&taskID](const auto& tuple) {return std::get<0>(tuple)  == taskID;});
		pid_t taskPID = std::get<1>(*it1);

		if(it != status.end())
		{
			uint64_t cr_val = std::get<1>(*it);
			if(cr_val)
			{
				// cr_val will be 1 for the new critical apps
            	LinuxBase::get_cat()->add_task(2,taskPID);
				new_clos = 2;
			}
			else
			{
				// cr_val will be 0 for the new non-critical apps
            	LinuxBase::get_cat()->add_task(1,taskPID);
				new_clos = 1;
			}
			//update taskIsInCRCLOS
			auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&taskID](const auto& tuple) {return std::get<0>(tuple)  == taskID;});
			it2 = taskIsInCRCLOS.erase(it2);
			taskIsInCRCLOS.push_back(std::make_pair(taskID,new_clos));
		}
		else
		{
			// Application has not suffered criticality change
			// Return non-critical app(s) in CLOS 3 or 5 to CLOS 1
			uint64_t clos_value = std::get<1>(item);
			//if((clos_value == 3) | (clos_value == 5))
			if( clos_value == 3 )
			{
				LOGINF("CLOS {}"_format(clos_value));
				LinuxBase::get_cat()->add_task(1,taskPID);
				auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&taskID](const auto& tuple) {return std::get<0>(tuple)  == taskID;});
				it2 = taskIsInCRCLOS.erase(it2);
				taskIsInCRCLOS.push_back(std::make_pair(taskID,1));
				LOGINF("Task {} returned to CLOS 1"_format(taskID));
			}
		}
	}

	// 3. Assign preconfigured masks to CLOSes 1 and 2
	switch(num_critical_new)
	{
		case 1:
			maskCrCLOS = 0xfff00;
			maskNonCrCLOS = 0x003ff;
			break;
		case 2:
			maskCrCLOS = 0xfff80;
            maskNonCrCLOS = 0x001ff;
			break;
		case 3:
			maskCrCLOS = 0xfffc0;
            maskNonCrCLOS = 0x000ff;
		default:
			break;
	}
	LinuxBase::get_cat()->set_cbm(1,maskNonCrCLOS);
	LinuxBase::get_cat()->set_cbm(2,maskCrCLOS);
	num_ways_CLOS_1 = __builtin_popcount(LinuxBase::get_cat()->get_cbm(1));
	num_ways_CLOS_2 = __builtin_popcount(LinuxBase::get_cat()->get_cbm(2));
	num_shared_ways = 2;
	LOGINF("YYY CLOS 1 (non-CR) has mask {:#x} ({} ways)"_format(LinuxBase::get_cat()->get_cbm(1),num_ways_CLOS_1));
	LOGINF("YYY CLOS 2 (CR) has mask {:#x} ({} ways)"_format(LinuxBase::get_cat()->get_cbm(2),num_ways_CLOS_2));
}

/*
 * Main method of the Critical-Aware V2 Policy
 */
void CriticalAwareV2::apply(uint64_t current_interval, const tasklist_t &tasklist)
{
	LOGINF("CAT Policy name: Critical-Aware V2");
	LOGINF("Current_interval = {}"_format(current_interval));

	// Apply only when the amount of intervals specified has passed
    if (current_interval % every != 0)
    	return;

    // Check if current interval must be left idle
	if(idle)
    {
    	LOGINF("Idle interval {}"_format(idle_count));
        idle_count = idle_count - 1;
        if(idle_count == 0)
        {
        	idle = false;
            idle_count = IDLE_INTERVALS;
        }
        return;
    }

	/** LOCAL VARIABLES DECLARATION**/
    // (pid_t, double) vectors of tuples
    auto v_ipc = std::vector<pairD_t>();
    auto v_l3_occup_mb = std::vector<pairD_t>();
	auto v_mpkil3 = std::vector<pairD_t>();
	auto all_mpkil3 = std::set<double>();

	// Vector with current active tasks
	auto active_tasks = std::vector<pid_t>();

	// Vector with outlier values (1 == outlier, 0 == not outlier)
    auto outlier = std::vector<pair_t>();

	// Apps that have changed to  critical (1) or to non-critical (0)
	auto status = std::vector<pair_t>();

    double ipcTotal = 0;
	double l3_occup_mb_total = 0;
    // Total IPC of critical applications
	double ipc_CR = 0;
	// Total IPC of non-critical applications
    double ipc_NCR = 0;

    // Number of critical apps found in the interval
    uint32_t critical_apps = 0;
	// Flag that set to true when number of critical apps detected has changed from previous
    bool change_in_outliers = false;

	uint32_t idTask;
	// Maximum value of MPKI-L3 in the current interval
	//double max_mpkil3 = 0;

    // Gather data for each task
    for (const auto &task_ptr : tasklist)
    {
    	const Task &task = *task_ptr;
        std::string taskName = task.name;
        pid_t taskPID = task.pid;
		uint32_t taskID = task.id;
        uint32_t cpu = task.cpus.front();

        // Obtain stats per interval
        uint64_t l3_miss = task.stats.last("mem_load_uops_retired.l3_miss");
        uint64_t inst = task.stats.last("instructions");
        double ipc = task.stats.last("ipc");
        double l3_occup_mb = task.stats.last("intel_cqm/llc_occupancy/") / 1024 / 1024;

        double MPKIL3 = (double)(l3_miss*1000) / (double)inst;

		ipcTotal += ipc;
		l3_occup_mb_total += l3_occup_mb;

        LOGINF("Task {} ({}): IPC = {}, MPKI_L3 = {}, l3_occup_mb {}"_format(taskName,taskID,ipc,MPKIL3,l3_occup_mb));
        v_ipc.push_back(std::make_pair(taskID, ipc));
        v_l3_occup_mb.push_back(std::make_pair(taskID, l3_occup_mb));
        pid_CPU.push_back(std::make_pair(taskID, cpu));
		id_pid.push_back(std::make_pair(taskID, taskPID));
		//active_tasks.push_back(id);

		// Update queue of each task with last value of MPKI-L3
		// Do not consider the excluded application
		auto it = deque_mpkil3.find(taskID);
		auto it2 = valid_mpkil3.find(taskID);

        if(it != deque_mpkil3.end())
		{
			std::deque<double> deque_aux = it->second;
			std::deque<double> deque_valid = it2->second;
			// Remove one value if size is equal to sliding window size
			if (deque_valid.size() == 10)
				deque_valid.pop_back();

			deque_aux.push_front(MPKIL3);

			double insert_value;
			if(deque_aux.size() == 3)
			{
				//LOGINF("{}: {} {} {}"_format(taskPID,deque_aux[0],deque_aux[1],deque_aux[2]));

				// Check middle value is not a spike
				if ((deque_aux[1] >= 2*deque_aux[0]) & (deque_aux[1] >= 2*deque_aux[2]))
				{
					// Middle value is a spike -> remove middle value and insert last value to all_mpkil3
					//all_mpkil3.insert(deque_aux[2]);
					insert_value = deque_aux[2];
					deque_valid.push_front(insert_value);
					deque_aux.pop_back(); //remove value 2
					deque_aux.pop_back(); //remove value 1
				}
				else
				{
					// Middle value is not a spike value
					//double aux = (deque_aux[1] + deque_aux[2]) / 2;
					insert_value = deque_aux[2];
					//all_mpkil3.insert(deque_aux[2]);
					deque_valid.push_front(insert_value);
					deque_aux.pop_back(); //remove value 2
				}

				// Add value to current mpkil3 vector
				v_mpkil3.push_back(std::make_pair(taskID,insert_value));

				// Add value to mpkil3_prev vector
				auto it_pm = std::find_if(v_mpkil3_prev.begin(), v_mpkil3_prev.end(),[&taskID](const auto& tuple) {return std::get<0>(tuple) == taskID;});
                if(it_pm == v_mpkil3_prev.end())
                	v_mpkil3_prev.push_back(std::make_pair(taskID,insert_value));
                else
                    std::get<1>(*it_pm) = insert_value;


			}
			else if(current_interval >= firstInterval)
			{
				// Get previous valid value of MPKI-L3 if no new values available
				auto it_pm = std::find_if(v_mpkil3_prev.begin(), v_mpkil3_prev.end(),[&taskID](const auto& tuple) {return std::get<0>(tuple) == taskID;});
              	if(it_pm != v_mpkil3_prev.end())
					v_mpkil3.push_back(std::make_pair(taskID,std::get<1>(*it_pm)));

			}

			// Store queue modified in the dictionary
			deque_mpkil3[taskID] = deque_aux;
			valid_mpkil3[taskID] = deque_valid;
		}
        else
        {
			// Add a new entry in the dictionary
            deque_mpkil3[taskID].push_front(MPKIL3);
			valid_mpkil3[taskID] = std::deque<double>();

			// Add in vector in case this apps has been restarted
			v_mpkil3.push_back(std::make_pair(taskID,MPKIL3));
        }

	}

	LOGINF("Total L3 occupation: {}"_format(l3_occup_mb_total));
	assert(l3_occup_mb_total > 0);

	// Perform no further action if cache-warmup time has not passed
    if (current_interval < firstInterval)
		return;

	// Check if taskIsInCRCLOS holds only current tasks
	// aux = vector to store reset values
	/*auto aux = std::vector<pair_t>();
	for (const auto &item : taskIsInCRCLOS)
	{
		pid_t taskPID = std::get<0>(item);
		uint64_t CLOS_val =std::get<1>(item);
		if ( std::find(active_tasks.begin(), active_tasks.end(), taskPID) == active_tasks.end() )
        {
			LOGINF("TASK {} HAS BEEN RESTARTED "_format(taskPID));
			// Save task no longer active
			aux.push_back(std::make_pair(taskPID,CLOS_val));
		}
	}

	if (!aux.empty())
	{
		// Remove tasks no longer active from taskIsInCRCLOS
		for (const auto &item : aux)
		{
			pid_t taskPID = std::get<0>(item);
			auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&taskPID](const auto& tuple) {return std::get<0>(tuple)  == taskPID;});
			it2 = taskIsInCRCLOS.erase(it2);

			// Erase entries of old PID from dictionary
			deque_mpkil3.erase(taskPID);
			valid_mpkil3.erase(taskPID);

			// Erase value if in previous critical
			//auto it3 = ipc_critical_prev.find(taskPID);
  			//if (it3 != ipc_critical_prev.end())
    		//	ipc_critical_prev.erase(it3);

		}
		aux.clear();

		// Add new active tasks to taskIsInCRCLOS
		for (const auto &item : active_tasks)
		{
			pid_t taskPID = item;
			auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&taskPID](const auto& tuple) {return std::get<0>(tuple)  == taskPID;});
			if(it2 == taskIsInCRCLOS.end())
			{
				// Check CLOS value of task
				uint64_t CLOS_val = LinuxBase::get_cat()->get_clos_of_task(taskPID);

				// Check is restarted app is the excluded app
				//if (CLOS_val == 4)
				//{
				//	excluded_application = taskPID;
				//	LOGINF("Exluded app has been restarted --> taskPID updated");
				//}

				// Add new pair
				taskIsInCRCLOS.push_back(std::make_pair(taskPID,CLOS_val));
				LOGINF("RESTARTED TASK {} in CLOS {} HAS BEEN ADDED to taskIsInCRCLOS"_format(taskPID,CLOS_val));
			}
		}
	}*/

	// Add values of MPKIL3 from each app to the set
	for (auto const &x : valid_mpkil3)
	{
		// Get deque
		std::deque<double> val = x.second;
		// Add values
		std::string res;
		for (auto i = val.cbegin(); i != val.cend(); ++i)
		{
			res = res + std::to_string(*i) + " ";
			all_mpkil3.insert(*i);
		}
		LOGINF(res);
	}

	if((current_interval > firstInterval) & (max_mpkil3 > 0))
	{
		// Remove all values greater than limit outlier
		LOGINF("Max. MPKIL3 of previous interval {}"_format(max_mpkil3));
    	LOGINF("RRR Size before erase:{}"_format(all_mpkil3.size()));
    	std::set<double>::iterator itN = all_mpkil3.upper_bound(max_mpkil3);
   	 	all_mpkil3.erase(itN,all_mpkil3.end());
    	LOGINF("RRR Size after erase:{}"_format(all_mpkil3.size()));
	}

	// Calculate IQR = Q3 - Q1
	int size = all_mpkil3.size();
	LOGINF("Size:{}, Q1 pos:{}, Q3 pos:{}"_format(size,size/4,size*0.75));
	double Q1 = *std::next(all_mpkil3.begin(), size/4);
	double Q3 = *std::next(all_mpkil3.begin(), size*0.75);
	double IQR = Q3 - Q1;
	LOGINF("Q1 = {}  |||  Q3 ={}"_format(Q1,Q3));

	// Calculate limit outlier
	double limit_outlier = Q3 + (IQR * 1.5);
    LOGINF("limit_outlier = {}"_format(limit_outlier));

	// Clear set
	all_mpkil3.clear();
	max_mpkil3 = 0;

    // Check if MPKI-L3 of each APP is 2 stds o more higher than the mean MPKI-L3
    for (const auto &item : v_mpkil3)
    {
    	double MPKIL3Task = std::get<1>(item);
		//if(MPKIL3Task > max_mpkil3)
		//	max_mpkil3 = MPKIL3Task;

        idTask = std::get<0>(item);
        int freqCritical = -1;
        double fractionCritical = 0;

		// Do not consider excluded_application
		if(1)
		{

        	if(current_interval > firstInterval)
        	{
            	// Search for mi tuple and update the value
            	auto it = frequencyCritical.find(idTask);
            	if(it != frequencyCritical.end())
                	freqCritical = it->second;
            	else
            	{
                	LOGINF("TASK RESTARTED --> INCLUDE IT AGAIN IN frequencyCritical");
                	frequencyCritical[idTask] = 0;
                	freqCritical = 0;
            	}
            	assert(freqCritical>=0);
            	fractionCritical = freqCritical / (double)(current_interval-firstInterval);
        	}

        	if (MPKIL3Task >= limit_outlier)
        	{
            	LOGINF("The MPKI_L3 of task with id {} is an outlier, since {} >= {}"_format(idTask,MPKIL3Task,limit_outlier));
            	outlier.push_back(std::make_pair(idTask,1));
            	critical_apps += 1;
            	frequencyCritical[idTask]++;
				if((max_mpkil3 == 0) | (MPKIL3Task < max_mpkil3))
					max_mpkil3 = MPKIL3Task;

        	}
        	else if((MPKIL3Task < limit_outlier) && (fractionCritical>=0.5))
        	{
				LOGINF("The MPKI_L3 of task with id {} is NOT an outlier, since   {} < {}"_format(idTask,MPKIL3Task,limit_outlier));
            	LOGINF("Fraction critical of {} is {} --> CRITICAL"_format(idTask, fractionCritical));
            	outlier.push_back(std::make_pair(idTask,1));
            	critical_apps += 1;
        	}
        	else
        	{
            	// It is not a critical app
            	LOGINF("The MPKI_L3 of task with id {} is NOT an outlier, since {} < {}"_format(idTask,MPKIL3Task,limit_outlier));
            	outlier.push_back(std::make_pair(idTask,0));

            	// Initialize counter if it's the first interval
            	if(current_interval == firstInterval)
            	{
                	frequencyCritical[idTask] = 0;
            	}
        	}
		}
    } // End for loop

    LOGINF("critical_apps = {}"_format(critical_apps));

	// Keep count of consecutive intervals no critical apps are detected
	/*if (critical_apps == 0)
		num_no_critical += 1;
	else
		num_no_critical = 0;

	// If during 5 or more consecutive intervals no critical apps are detected
	if(num_no_critical >= 5)
	{
		// Then remove from all_mpkil3 the values greater than max_mpkil3
		LOGINF("Number of intervals with no critical apps >= 5!!");
		LOGINF("RRR Size before erase:{}"_format(all_mpkil3.size()));
    	std::set<double>::iterator itN = all_mpkil3.upper_bound(max_mpkil3);
		all_mpkil3.erase(itN,all_mpkil3.end());
		LOGINF("RRR Size after erase:{}"_format(all_mpkil3.size()));
	}*/

	// If no previous configuration has been established (firstTime = true)
    if (firstTime)
    {
    	// Set ways of CLOS 1 and 2
        switch ( critical_apps )
        {
            case 1:
            	// 1 critical app = 12cr10others
                maskCrCLOS = 0xfff00;
                maskNonCrCLOS = 0x003ff;
                state = 1;
                break;
            case 2:
                // 2 critical apps = 13cr9others
                maskCrCLOS = 0xfff80;
                maskNonCrCLOS = 0x001ff;
                state = 2;
                break;
            case 3:
                // 3 critical apps = 14cr8others
                maskCrCLOS = 0xfffc0;
                maskNonCrCLOS = 0x000ff;
                state = 3;
                break;
            default:
                // no critical apps or more than 3 = 20cr20others
                maskCrCLOS = 0xfffff;
                maskNonCrCLOS = 0xfffff;
                state = 4;
                break;
		} // close switch

		// Set masks to each CLOS
        LinuxBase::get_cat()->set_cbm(1,maskNonCrCLOS);
        LinuxBase::get_cat()->set_cbm(2,maskCrCLOS);

		num_ways_CLOS_1 = __builtin_popcount(LinuxBase::get_cat()->get_cbm(1));
        num_ways_CLOS_2 = __builtin_popcount(LinuxBase::get_cat()->get_cbm(2));
		assert((num_ways_CLOS_1>0) & (num_ways_CLOS_1<=20));
		assert((num_ways_CLOS_2>0) & (num_ways_CLOS_2<=20));
		num_shared_ways = (num_ways_CLOS_2 + num_ways_CLOS_1) - 20;

        LOGINF("CLOS 2 (CR) now has mask {:#x}"_format(maskCrCLOS));
        LOGINF("CLOS 1 (non-CR) now has mask {:#x}"_format(maskNonCrCLOS));

		assert(num_shared_ways >= 0);

        // Assign each task to its corresponding CLOS
        for (const auto &item : outlier)
        {
            idTask = std::get<0>(item);
            uint32_t outlierValue = std::get<1>(item);
            auto it = std::find_if(v_ipc.begin(), v_ipc.end(),[&idTask](const auto& tuple) {return std::get<0>(tuple) == idTask;});
            double ipcTask = std::get<1>(*it);
			auto it1 = std::find_if(id_pid.begin(), id_pid.end(),[&idTask](const auto& tuple) {return std::get<0>(tuple)  == idTask;});
         	pid_t pidTask = std::get<1>(*it1);

			if(outlierValue)
			{
                LinuxBase::get_cat()->add_task(2,pidTask);
				LOGINF("Task ID {} assigned to CLOS 2"_format(idTask));
				taskIsInCRCLOS.push_back(std::make_pair(idTask,2));
                ipc_CR += ipcTask;
				// Add value of IPC to dictionary
				ipc_critical_prev[idTask] = ipcTask;
			}
			else
			{
				LinuxBase::get_cat()->add_task(1,pidTask);
				LOGINF("Task ID {} assigned to CLOS 1"_format(idTask));
				taskIsInCRCLOS.push_back(std::make_pair(idTask,1));
                ipc_NCR += ipcTask;
			}
		}

		// Set flag to 0
        firstTime = 0;

	} // Not first time
	else
	{
		// Check if there is a new critical app
		// Or an app that is no longer critical
		for (const auto &item : outlier)
		{
			idTask = std::get<0>(item);
            uint32_t outlierValue = std::get<1>(item);
			assert((outlierValue == 0) | (outlierValue == 1));

			auto itW = std::find_if(v_ipc.begin(), v_ipc.end(),[&idTask](const  auto& tuple) {return std::get<0>(tuple) == idTask;});
			double ipcTask = std::get<1>(*itW);

			auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&idTask](const auto& tuple) {return std::get<0>(tuple)  == idTask;});
            uint64_t CLOSvalue = std::get<1>(*it2);
			LOGINF("CLOSvalue = {}"_format(CLOSvalue));
			assert((CLOSvalue >= 1) && (CLOSvalue <= 5));

			if(outlierValue && (CLOSvalue % 2 != 0))
            {
                LOGINF("There is a new critical app (outlier {}, current CLOS {})"_format(outlierValue,CLOSvalue));
                change_in_outliers = true;
				status.push_back(std::make_pair(idTask,1));
				ipc_critical_prev[idTask] = ipcTask;
            }
			else if(!outlierValue && (CLOSvalue == 2))
            {
            	LOGINF("There is a critical app that is no longer critical)");
                change_in_outliers = true;
				status.push_back(std::make_pair(idTask,0));
            }
            else if(outlierValue)
				ipc_CR += ipcTask;
            else
                ipc_NCR += ipcTask;
        }

		// Update configuration if there is a change in the number of critical apps
        if(change_in_outliers)
		{
			reset_configuration(tasklist);
            //update_configuration(taskIsInCRCLOS, status,prev_critical_apps,critical_apps);
		}
		else
        {
			// If there is no new critical app, modify masks
            if((critical_apps > 0) && (critical_apps < 4))
            {
				/*nt64_t n_apps = 0;
				double total_space = 0;

                for (const auto &item : outlier)
                {
                    pidTask = std::get<0>(item);
                    uint32_t outlierValue = std::get<1>(item);

					// Find L3 Occupancy
					auto itT = std::find_if(v_l3_occup_mb.begin(), v_l3_occup_mb.end(),[&pidTask](const auto& tuple) {return std::get<0>(tuple) == pidTask;});
					double l3_occup_mb_task = std::get<1>(*itT);

					if(outlierValue)
					{
						// Check if the occupancy of critical app(s) is close to the num. of ways assigned (>=85%)
						// If not, perform no action (return)
						if((critical_apps == 1) && (l3_occup_mb_task <= 0.85*num_ways_CLOS_2))
						{
							LOGINF("Task {} ONLY occupies {} when it has {} available --> RETURN"_format(pidTask,l3_occup_mb_task,num_ways_CLOS_2));
							return;
						}
						else if(critical_apps == 2)
						{
							total_space += l3_occup_mb_task;
							n_apps += 1;
							if(n_apps == 2 && (total_space <= 0.85*num_ways_CLOS_2))
							{
								LOGINF("ONLY occupied {} when there is {} available --> RETURN"_format(total_space,num_ways_CLOS_2));
								return;
							}
						}
						else if(critical_apps == 3)
						{
							total_space += l3_occup_mb_task;
                            n_apps += 1;
                            if(n_apps == 3 && (total_space <= 0.85*num_ways_CLOS_2))
                            {
                                LOGINF("ONLY occupied {} when there is {} available --> RETURN"_format(total_space,num_ways_CLOS_2));
                                return;
                            }
						}
					}
					else if(!outlierValue && CLOS_key <= 3)
                    {
                        auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&pidTask](const auto& tuple) {return std::get<0>(tuple) == pidTask;});
                        uint64_t CLOSvalue = std::get<1>(*it2);
						assert((CLOSvalue >= 1) && (CLOSvalue <= 3));

						// Check if there is a non-critical application occupying more space than it should
						if((l3_occup_mb_task > 3) && (CLOSvalue == 1))
						{
							// Isolate it in a separate CLOS with two exclusive ways
							uint64_t new_mask = clos_mask[CLOS_key];
							LinuxBase::get_cat()->set_cbm(CLOS_key,new_mask);
							LinuxBase::get_cat()->add_task(CLOSvalue,pidTask);
							LOGINF("Task {} has l3_occup_mb {}, therefore it has been assigned to CLOS {} with mask {:x}"_format(pidTask,l3_occup_mb_task,CLOS_key,new_mask));

							// Remove 2 ways from CLOS 1 (hosting non-critical apps)
							uint64_t new_mask_ncr = (maskNonCrCLOS << 2) & maskNonCrCLOS;
							LOGINF("New mask of ncr clos: {:x}"_format(new_mask_ncr));
							maskNonCrCLOS = new_mask_ncr;
							LinuxBase::get_cat()->set_cbm(1,maskNonCrCLOS);
							num_ways_CLOS_1 = __builtin_popcount(LinuxBase::get_cat()->get_cbm(1));

							// Update taskIsInCRCLOS
							it2 = taskIsInCRCLOS.erase(it2);
							taskIsInCRCLOS.push_back(std::make_pair(pidTask,CLOS_key));

							// Leave some idle intervals for apps to have time to ocuppy new space
							idle = true;

							CLOS_key += 2;
						}
					} // End if(outlierValue)

				}*/ //End for loop

				//std::map<pid_t,double>::iterator itc;

				// Check if a NEW critical app is not making profitable use of the space
				// This will be the case if the IPC of the critical application
				// has not improved more than 3%
				/*for ( itc = ipc_critical_prev.begin(); itc != ipc_critical_prev.end(); itc++ )
				{
					pidTask = itc->first;
					double ipc_prev = itc->second;

					auto itW = std::find_if(v_ipc.begin(), v_ipc.end(),[&pidTask](const  auto& tuple) {return std::get<0>(tuple) == pidTask;});
              		double ipcTask = std::get<1>(*itW);

					if((!false_critical_app) && (ipcTask < 1.3*ipc_prev))
					{
						// Assign app to an isolated CLOS with 2 ways
						LinuxBase::get_cat()->set_cbm(4,0x0000C);
						LinuxBase::get_cat()->add_task(4,pidTask);
						uint64_t c = LinuxBase::get_cat()->get_clos_of_task(pidTask);
						LOGINF("!!!! TASK {} isolated in CLOS {} !!!"_format(pidTask,c));

						// Make app task no longer eligible to be critical
						excluded_application = pidTask;

						// Update taskIsInCRCLOS
						auto it2 = std::find_if(taskIsInCRCLOS.begin(), taskIsInCRCLOS.end(),[&pidTask](const auto& tuple) {return std::get<0>(tuple)  == pidTask;});
						it2 = taskIsInCRCLOS.erase(it2);
						taskIsInCRCLOS.push_back(std::make_pair(pidTask,4));

						false_critical_app = true;

						// Update CR-related variables
						critical_apps -= 1;
						ipc_CR -= ipcTask;
					}
				}

				// Delete all values of dictionary
				ipc_critical_prev.clear();

				// Check status of isolated application
				if(false_critical_app)
				{
					pidTask = excluded_application;
					// Find current IPC
					auto itW = std::find_if(v_ipc.begin(), v_ipc.end(),[&pidTask](const  auto& tuple) {return std::get<0>(tuple) == pidTask;});
              		double ipcEXcl = std::get<1>(*itW);
					uint64_t c = LinuxBase::get_cat()->get_clos_of_task(excluded_application);

					// Check if false_critical app is worse
					if(ipcEXcl < 0.7*excluded_application_ipc)
						LOGINF("XX Isolated app {} in CLOS {} is BAD ({} compared to {})"_format(excluded_application,c,ipcEXcl,excluded_application_ipc));
					else
						LOGINF("XX Isolated app {} in CLOS {} is GOOD ({} compared to {})"_format(excluded_application,c,ipcEXcl,excluded_application_ipc));

					assert(c==4);

					// update ipc
					excluded_application_ipc = ipcEXcl;
				}*/



				LOGINF("IPC total {} vs.Expected IPC total {}"_format(ipcTotal,expectedIPCtotal));
				double UP_limit_IPC = expectedIPCtotal * 1.04;
				double LOW_limit_IPC = expectedIPCtotal  * 0.96;
				double NCR_limit_IPC = ipc_NCR_prev*0.96;
				double CR_limit_IPC = ipc_CR_prev*0.96;

				if(ipcTotal > UP_limit_IPC)
					LOGINF("New IPC is BETTER: IPCtotal {} > {}"_format(ipcTotal,UP_limit_IPC));
				else if((ipc_CR < CR_limit_IPC) && (ipc_NCR >= NCR_limit_IPC))
					LOGINF("WORSE CR IPC: CR {} < {} && NCR {} >= {}"_format(ipc_CR,CR_limit_IPC,ipc_NCR,NCR_limit_IPC));
				else if((ipc_NCR < NCR_limit_IPC) && (ipc_CR >= CR_limit_IPC))
					LOGINF("WORSE NCR IPC: NCR {} < {} && CR {} >= {}"_format(ipc_NCR,NCR_limit_IPC,ipc_CR,CR_limit_IPC));
				else if( (ipc_CR < CR_limit_IPC) && (ipc_NCR < NCR_limit_IPC))
					LOGINF("BOTH IPCs are WORSE: CR {} < {} && NCR {} < {}"_format(ipc_CR,CR_limit_IPC,ipc_NCR,NCR_limit_IPC));
				else
					LOGINF("BOTH IPCs are EQUAL (NOT WORSE)");

				//transitions switch-case
				switch (state)
                {
                	case 1: case 2: case 3:
                        if(ipcTotal > UP_limit_IPC)
                            idle = true;
                        else if((ipcTotal <= UP_limit_IPC) && (ipcTotal >= LOW_limit_IPC))
                            state = 5;
                     	else if((ipc_NCR < NCR_limit_IPC) && (ipc_CR >= CR_limit_IPC))
                            state = 6;
                        else if((ipc_CR < CR_limit_IPC) && (ipc_NCR >= NCR_limit_IPC))
                            state = 5;
                        else
                            state = 5;
                        break;

                    case 5: case 6:
                        if(ipcTotal > UP_limit_IPC)
                            idle = true;
                        else if( (ipcTotal <= UP_limit_IPC) && (ipcTotal >= LOW_limit_IPC))
                            state = 8;
                        else if((ipc_NCR < NCR_limit_IPC) && (ipc_CR >= CR_limit_IPC))
                            state = 7;
                        else if((ipc_CR < CR_limit_IPC) && (ipc_NCR >= NCR_limit_IPC))
                            state = 8;
                        else // NCR and CR worse
                            state = 8;
                        break;

                    case 7: case 8:
                        if(ipcTotal > UP_limit_IPC)
                            idle = true;
                        else if((ipcTotal <= UP_limit_IPC) && (ipcTotal >= LOW_limit_IPC))
                            state = 5;
                        else if((ipc_NCR < NCR_limit_IPC) && (ipc_CR >= CR_limit_IPC))
                            state = 6;
                        else if((ipc_CR < CR_limit_IPC) && (ipc_NCR >= NCR_limit_IPC))
                            state = 5;
                        else // NCR and CR worse
                            state = 5;
                        break;
                }

				/*switch (state)
				{
					case 1: case 2: case 3: case 7: case 8:
						if((ipcTotal > UP_limit_IPC) | (equal))
							idle = true;
						else if((ipc_NCR < NCR_limit_IPC) && (ipc_CR >= CR_limit_IPC))
							state = 6;
						else
							state = 5;
						break;
					case 5: case 6:
						if(ipcTotal > UP_limit_IPC)
							idle = true;
						else if((ipc_NCR < NCR_limit_IPC) && (ipc_CR >= CR_limit_IPC))
							state = 7;
						else
							state = 8;
						break;
				}*/

				// State actions switch-case
				if(idle)
					LOGINF("New IPC is better or equal -> {} idle intervals"_format(IDLE_INTERVALS));
				else
				{
					switch ( state )
					{
						case 5:
							LOGINF("NCR-- (Remove one shared way from CLOS with non-critical apps)");
							maskNonCrCLOS = (maskNonCrCLOS >> 1) & maskNonCrCLOS;
							assert(maskNonCrCLOS != 0x00000);
							LinuxBase::get_cat()->set_cbm(1,maskNonCrCLOS);
							break;
						case 6:
							LOGINF("CR-- (Remove one shared way from CLOS with critical apps)");
							maskCrCLOS = (maskCrCLOS << 1) & maskCrCLOS;
							assert(maskCrCLOS != 0x00000);
							LinuxBase::get_cat()->set_cbm(2,maskCrCLOS);
							break;
						case 7:
							LOGINF("NCR++ (Add one shared way to CLOS with non-critical apps)");
							maskNonCrCLOS = (maskNonCrCLOS << 1) | maskNonCrCLOS;
							assert(maskNonCrCLOS != 0x00000);
							LinuxBase::get_cat()->set_cbm(1,maskNonCrCLOS);
							break;
						case 8:
							LOGINF("CR++ (Add one shared way to CLOS with critical apps)");
							maskCrCLOS = (maskCrCLOS >> 1) | maskCrCLOS;
							assert(maskCrCLOS != 0x00000);
							LinuxBase::get_cat()->set_cbm(2,maskCrCLOS);
							break;
						default:
							break;
					}
					num_ways_CLOS_1 = __builtin_popcount(LinuxBase::get_cat()->get_cbm(1));
					num_ways_CLOS_2 = __builtin_popcount(LinuxBase::get_cat()->get_cbm(2));

					LOGINF("COS 2 (CR)     has mask {:#x} ({} ways)"_format(LinuxBase::get_cat()->get_cbm(2),num_ways_CLOS_2));
					LOGINF("COS 1 (non-CR) has mask {:#x} ({} ways)"_format(LinuxBase::get_cat()->get_cbm(1),num_ways_CLOS_1));

					int64_t aux_ns = (num_ways_CLOS_2 + num_ways_CLOS_1) - 20;
					num_shared_ways = (aux_ns < 0) ? 0 : aux_ns;
					LOGINF("Number of shared ways: {}"_format(num_shared_ways));
					assert(num_shared_ways >= 0);

				}

			}
		}
	} //end first time
	ipc_CR_prev = ipc_CR;
	ipc_NCR_prev = ipc_NCR;

	// Assign total IPC of this interval to previos value
	expectedIPCtotal = ipcTotal;
	prev_critical_apps = critical_apps;
	id_pid.clear();

}//end apply



void tasks_to_closes(catlinux_ptr_t cat, const tasklist_t &tasklist, const clusters_t &clusters)
{
	assert(cat->get_max_closids() >= clusters.size());

	for(size_t clos = 0; clos < clusters.size(); clos++)
	{
		for (const auto point : clusters[clos].getPoints())
		{
			const auto &task = tasks_find(tasklist, point->id);
			cat->add_task(clos, task->pid);
		}
	}
}





// Given a cluster, return a pretty string similar to: "id1:app1, id2:app2"
std::string cluster_to_tasks(const Cluster &cluster, const tasklist_t &tasklist)
{
	std::string task_ids;
	const auto &points = cluster.getPoints();
	size_t p = 0;
	for (const auto &point : points)
	{
		const auto &task = tasks_find(tasklist, point->id);
		task_ids += "{}:{}"_format(task->id, task->name);
		task_ids += (p == points.size() - 1) ? "" : ", ";
		p++;
	}
	return task_ids;
}


// Assign each task to a cluster
clusters_t ClusteringBase::apply(const tasklist_t &tasklist)
{
	auto clusters = clusters_t();
	for (const auto &task_ptr : tasklist)
	{
		const Task &task = *task_ptr;
		clusters.push_back(Cluster(task.id, {0}));
		clusters[task.id].addPoint(
				std::make_shared<Point>(task.id, std::vector<double>{0}));
	}
	return clusters;
}


clusters_t Cluster_SF::apply(const tasklist_t &tasklist)
{
	// (Pos, Stalls) tuple
	typedef std::pair<pid_t, uint64_t> pair_t;
	auto v = std::vector<pair_t>();

	for (const auto &task : tasklist)
	{
		uint64_t stalls;
		try
		{
			stalls = acc::sum(task->stats.events.at("cycle_activity.stalls_ldm_pending"));
		}
		catch (const std::exception &e)
		{
			std::string msg = "This policy requires the event 'cycle_activity.stalls_ldm_pending'. The events monitorized are:";
			for (const auto &kv : task->stats.events)
				msg += "\n" + kv.first;
			throw_with_trace(std::runtime_error(msg));
		}
		v.push_back(std::make_pair(task->id, stalls));
	}

	// Sort in descending order by stalls
	std::sort(begin(v), end(v),
			[](const pair_t &t1, const pair_t &t2)
			{
				return std::get<1>(t1) > std::get<1>(t2);
			});

	// Assign tasks to clusters
	auto clusters = clusters_t();
	size_t t = 0;
	for (size_t s = 0; s < sizes.size(); s++)
	{
		auto size = sizes[s];
		clusters.push_back(Cluster(s, {0}));
		assert(size > 0);
		while (size > 0)
		{
			auto task_id = v[t].first;
			auto task_stalls = v[t].second;
			clusters[s].addPoint(std::make_shared<Point>(task_id, std::vector<double>{(double) task_stalls}));
			size--;
			t++;
		}
		clusters[s].updateMeans();
	}
	if (t != tasklist.size())
	{
		int diff = (int) t - (int) tasklist.size();
		throw_with_trace(std::runtime_error("This clustering policy expects {} {} tasks"_format(std::abs(diff), diff > 0 ? "more" : "less")));
	}
	return clusters;
}


clusters_t Cluster_KMeans::apply(const tasklist_t &tasklist)
{
	auto data = std::vector<point_ptr_t>();

	// Put data in the format KMeans expects
	for (const auto &task_ptr : tasklist)
	{
		const Task &task = *task_ptr;
		double metric;
		try
		{
			metric = acc::rolling_mean(task.stats.events.at(event));
			metric = std::floor(metric * 100 + 0.5) / 100; // Round positive numbers to 2 decimals
		}
		catch (const std::exception &e)
		{
			std::string msg = "This policy requires the event '{}'. The events monitorized are:"_format(event);
			for (const auto &kv : task.stats.events)
				msg += "\n" + kv.first;
			throw_with_trace(std::runtime_error(msg));
		}

		if (metric == 0)
			throw_with_trace(ClusteringBase::CouldNotCluster("The event '{}' value is 0 for task {}:{}"_format(event, task.id, task.name)));

		data.push_back(std::make_shared<Point>(task.id, std::vector<double>{metric}));
	}

	std::vector<Cluster> clusters;
	if (num_clusters > 0)
	{
		LOGDEB("Enforce {} clusters..."_format(num_clusters));
		KMeans::clusterize(num_clusters, data, clusters, 100);
	}
	else
	{
		assert(num_clusters == 0);
		LOGDEB("Try to find the optimal number of clusters...");
		KMeans::clusterize_optimally(max_clusters, data, clusters, 100, eval_clusters);
	}

	LOGDEB(fmt::format("Clusterize: {} points in {} clusters using the event '{}'", data.size(), clusters.size(), event));

	// Sort clusters in ASCENDING order
	if (sort_ascending)
	{
		std::sort(begin(clusters), end(clusters),
				[this](const Cluster &c1, const Cluster &c2)
				{
					return c1.getCentroid()[0] < c2.getCentroid()[0];
				});
	}

	// Sort clusters in DESCENDING order
	else
	{
		std::sort(begin(clusters), end(clusters),
				[this](const Cluster &c1, const Cluster &c2)
				{
					return c1.getCentroid()[0] > c2.getCentroid()[0];
				});
	}
	LOGDEB("Sorted clusters in {} order:"_format(sort_ascending ? "ascending" : "descending"));
	for (const auto &cluster : clusters)
		LOGDEB(cluster.to_string());

	return clusters;
}


cbms_t Distribute_N::apply(const tasklist_t &, const clusters_t &clusters)
{
	cbms_t ways(clusters.size(), -1);
	for (size_t i = 0; i < clusters.size(); i++)
	{
		ways[i] <<= ((i + 1) * n);
		ways[i] = cut_mask(ways[i]);
		if (ways[i] == 0)
			throw_with_trace(std::runtime_error("Too many CLOSes ({}) or N too big ({}) have resulted in an empty mask"_format(clusters.size(), n)));
	}
	return ways;
}


cbms_t Distribute_RelFunc::apply(const tasklist_t &, const clusters_t &clusters)
{
	cbms_t cbms;
	auto values = std::vector<double>();
	if (invert_metric)
		LOGDEB("Inverting metric...");
	for (size_t i = 0; i < clusters.size(); i++)
	{
		if (invert_metric)
			values.push_back(1 / clusters[i].getCentroid()[0]);
		else
			values.push_back(clusters[i].getCentroid()[0]);
	}
	double max = *std::max_element(values.begin(), values.end());

	for (size_t i = 0; i < values.size(); i++)
	{
		double x = values[i] / max;
		double y;
		assert(x >= 0 && x <= 1);
		const double a = std::log(max_ways - min_ways + 1);
		x *= a; // Scale X for the interval [0, a]
		y = std::exp(x) + min_ways - 1;
		int ways = std::round(y);
		cbms.push_back(cut_mask(~(-1 << ways)));

		LOGDEB("Cluster {} : x = {} y = {} -> {} ways"_format(i, x, y, ways));
	}
	return cbms;
}

void ClusterAndDistribute::show(const tasklist_t &tasklist, const clusters_t &clusters, const cbms_t &ways)
{
	assert(clusters.size() == ways.size());
	for (size_t i = 0; i < ways.size(); i++)
	{
		std::string task_ids;
		const auto &points = clusters[i].getPoints();
		size_t p = 0;
		for (const auto &point : points)
		{
			const auto &task = tasks_find(tasklist, point->id);
			task_ids += "{}:{}"_format(task->id, task->name);
			task_ids += (p == points.size() - 1) ? "" : ", ";
			p++;
		}
		LOGDEB(fmt::format("{{COS{}: {{mask: {:#7x}, num_ways: {:2}, tasks: [{}]}}}}", i, ways[i], __builtin_popcount(ways[i]), task_ids));
	}
}


void ClusterAndDistribute::apply(uint64_t current_interval, const tasklist_t &tasklist)
{
	// Apply the policy only when the amount of intervals specified has passed
	if (current_interval % every != 0)
		return;

	clusters_t clusters;
	try
	{
		clusters = clustering->apply(tasklist);
	}
	catch (const ClusteringBase::CouldNotCluster &e)
	{
		LOGWAR("Not doing any partitioning in interval {}: {}"_format(current_interval, e.what()));
		return;
	}
	auto ways = distributing->apply(tasklist, clusters);
	show(tasklist, clusters, ways);
	tasks_to_closes(LinuxBase::get_cat(), tasklist, clusters);
	set_cbms(ways);
}


void SquareWave::apply(uint64_t current_interval, const tasklist_t &tasklist)
{
	clusters_t clusters = clustering.apply(tasklist);
	assert(clusters.size() <= waves.size());

	// Adjust ways
	for (uint32_t clos = 0; clos < waves.size(); clos++)
	{
		cbm_t cbm = LinuxBase::get_cat()->get_cbm(clos);
		if (current_interval % waves[clos].interval == 0)
		{
			if (waves[clos].is_down)
			{
				cbm = waves[clos].down;
			}
			else
			{
				cbm = waves[clos].up;
			}
			waves[clos].is_down = !waves[clos].is_down;
			get_cat()->set_cbm(clos, cbm);
		}
		std::string task_str = "";
		if (clos < clusters.size())
			task_str = cluster_to_tasks(clusters[clos], tasklist);
		LOGDEB(fmt::format("{{clos{}: {{cbm: {:#7x}, num_ways: {:2}, tasks: [{}]}}}}", clos, cbm, __builtin_popcount(cbm), task_str));
	}

	tasks_to_closes(this->get_cat(), tasklist, clusters);
}

}} // cat::policy
