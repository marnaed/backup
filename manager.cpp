#include <algorithm>
#include <clocale>
#include <iostream>
#include <csignal>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/stacktrace.hpp>
#include <fmt/format.h>
#include <yaml-cpp/yaml.h>

#include <signal.h>
#include <setjmp.h>

#include "cat-intel.hpp"
#include "cat-linux.hpp"
#include "cat-policy.hpp"
#include "common.hpp"
#include "config.hpp"
#include "events-perf.hpp"
#include "log.hpp"
#include "stats.hpp"
#include "task.hpp"


namespace chr = std::chrono;
namespace fs = boost::filesystem;
namespace po = boost::program_options;

using std::string;
using std::to_string;
using std::vector;
using std::this_thread::sleep_for;
using std::cout;
using std::cerr;
using std::endl;
using fmt::literals::operator""_format;

typedef std::shared_ptr<CAT> CAT_ptr_t;
typedef std::chrono::system_clock::time_point time_point_t;


CAT_ptr_t cat_setup(const string &kind, const vector<Cos> &coslist);
void loop(tasklist_t &tasklist, std::shared_ptr<cat::policy::Base> catpol, Perf &perf, const vector<string> &events, uint64_t time_int_us, uint32_t max_int, std::ostream &out, std::ostream &ucompl_out, std::ostream &total_out);
void clean(tasklist_t &tasklist, CAT_ptr_t cat, Perf &perf);
[[noreturn]] void clean_and_die(tasklist_t &tasklist, CAT_ptr_t cat, Perf &perf);
std::string program_options_to_string(const std::vector<po::option>& raw);
void adjust_time(const time_point_t &start_int, const time_point_t &start_glob, const uint64_t interval, const uint64_t time_int_us, int64_t &adj_delay_us);
void herod_the_great();
void sigint_handler(int signum);
void sigabrt_handler(int signum);

// Signal
jmp_buf return_to_top_level;


CAT_ptr_t cat_setup(const string &kind, const vector<Cos> &coslist)
{
	LOGINF("Using {} CAT"_format(kind));
	std::shared_ptr<CAT> cat;
	if (kind == "intel")
	{
		cat = std::make_shared<CATIntel>();
	}
	else
	{
		assert(kind == "linux");
		cat = std::make_shared<CATLinux>();
	}
	cat->init();

	for (size_t i = 0; i < coslist.size(); i++)
	{
		const auto &cos = coslist[i];
		cat->set_cbm(i, cos.mask);
		for (const auto &cpu : cos.cpus)
			cat->add_cpu(i, cpu);
	}

	return cat;
}


void loop(
		tasklist_t &tasklist,
		sched::ptr_t &sched,
		std::shared_ptr<cat::policy::Base> catpol,
		Perf &perf,
		const vector<string> &events,
		uint64_t time_int_us,
		uint32_t max_int,
		std::ostream &out,
		std::ostream &ucompl_out,
		std::ostream &total_out)
{
	if (time_int_us <= 0)
		throw_with_trace(std::runtime_error("Interval time must be positive and greater than 0"));
	if (max_int <= 0)
		throw_with_trace(std::runtime_error("Max time must be positive and greater than 0"));

	// Prepare Perf to measure events and initialize stats
	for (const auto &task : tasklist)
		task->stats.init(perf.get_names(task->pid)[0]);

	// Print headers
	task_stats_print_headers(*tasklist[0], out);
	task_stats_print_headers(*tasklist[0], ucompl_out);
	task_stats_print_headers(*tasklist[0], total_out);

	// First reading of counters
	for (const auto &task : tasklist)
	{
		perf.enable_counters(task->pid);
		const counters_t counters = perf.read_counters(task->pid,catpol->get_cat())[0];
		task->stats.accum(counters);
	}

	// Loop
	uint32_t interval;
	int64_t adj_delay_us = time_int_us;
	auto start_glob = std::chrono::system_clock::now();
	auto t1 = std::chrono::system_clock::now(); //measure overhead algorithm
	auto t2 = std::chrono::system_clock::now();
	uint64_t total_elapsed_us = 0;

	tasklist_t runlist = tasklist_t(tasklist); // Tasks that are not done
	tasklist_t schedlist = tasklist_t(runlist);
	for (interval = 0; interval < max_int; interval++)
	{
		auto start_int = std::chrono::system_clock::now();
		bool all_completed = true; // Have all the tasks reached their execution limit?

		LOGINF("Starting interval {} - {} us"_format(interval, chr::duration_cast<chr::microseconds> (start_int - start_glob).count()));

		// Sleep
		t2 = std::chrono::system_clock::now();
		if (interval > 0)
		{
			uint64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>  (t2 - t1).count();
			uint32_t prev_interval = interval - 1;
			LOGINF("[OVERHEAD] Interval {} - {} = {} us"_format(interval,prev_interval,elapsed_us));
			total_elapsed_us = total_elapsed_us + elapsed_us;
		}
		tasks_resume(schedlist);
		sleep_for(chr::microseconds(adj_delay_us));
		tasks_pause(schedlist); // Status can change from runnable -> exited
		LOGDEB("Slept for {} us"_format(adj_delay_us));
		t1 = std::chrono::system_clock::now();

		// Control elapsed time
		adjust_time(start_int, start_glob, interval, time_int_us, adj_delay_us);

		// Get CPU of manager
		int cpu_manager =  get_self_cpu_id();
		LOGDEB("----> Manager is in CPU {}"_format(cpu_manager));

		// Process tasks...
		for (const auto &task_ptr : schedlist)
		{
			Task &task = *task_ptr;
			// Get CPU of task
			int cpu_id = get_cpu_id(task.pid);
			LOGINF("----> Task {} is in CPU {}"_format(task.pid,cpu_id));
			// Read stats
			const counters_t counters = perf.read_counters(task.pid, catpol->get_cat())[0];
			task.stats.accum(counters);

			// Test if the instruction limit has been reached
			if (task.max_instr > 0 && task.stats.get_current("instructions") >=  task.max_instr)
			{
				task.set_status(Task::Status::limit_reached); // Status can change from runnable -> limit_reached
				task.completed++;
			}

			// If any non-batch task is not completed then we don't finish
			if (!task.completed && !task.batch)
				all_completed = false;

			// Print interval stats
			task_stats_print_interval(task, interval, out);

			// It's the first time it finishes or reaches the instruction limit print acumulated stats until this point
			if (task.get_status() == Task::Status::limit_reached || task.get_status() == Task::Status::exited)
			{
				if (task.completed == 1)
					task_stats_print_total(task, interval, ucompl_out);
			}
		}

		// All the tasks have reached their limit -> finish execution
		if (all_completed)
		{
			LOGINF("[TOTAL OVERHEAD] {} us"_format(total_elapsed_us));
			break;
		}

		for (const auto &task_ptr : schedlist)
		{
			// Deal with apps that finish or reach the limit
			task_restart_or_set_done(*task_ptr, catpol->get_cat(), perf, events); // Status can change from (exited | limit_reached) -> done

			// If it's done print total stats
			if (task_ptr->get_status() == Task::Status::done)
				task_stats_print_total(*task_ptr, interval, total_out);
		}

		// Remove tasks that are done from runlist
		runlist.erase(std::remove_if(runlist.begin(), runlist.end(), [](const auto &task_ptr) { return task_ptr->get_status() == Task::Status::done; }), runlist.end());
		assert(!runlist.empty());

		// Select tasks for next interval execution
		schedlist = sched->apply(interval, runlist);
		assert(!schedlist.empty());

		LOGDEB(iterable_to_string(schedlist.begin(), schedlist.end(), [](const auto &t) {return "{}:{}[{}]({})"_format(t->id, t->name, sched::Status(t->pid)("Cpus_allowed_list"), sched::Stat(t->pid).processor);}, " "));

		// Adjust CAT according to the selected policy
		catpol->apply(interval, schedlist);
	}

	// Print acumulated stats for non completed tasks and total stats for all the tasks
	for (const auto &task : tasklist)
	{
		if (!task->completed)
		{
			task_stats_print_total(*task, interval, ucompl_out);
			task_stats_print_total(*task, interval, total_out);
		}
		if (task->get_status() != Task::Status::done)
			task_stats_print_total(*task, interval, total_out);
	}
}


void adjust_time(const time_point_t &start_int, const time_point_t &start_glob, const uint64_t interval, const uint64_t time_int_us, int64_t &adj_delay_us)
{
	// Control elapsed time
	uint64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>
			(std::chrono::system_clock::now() - start_int).count();
	uint64_t total_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>
			(std::chrono::system_clock::now() - start_glob).count();

	uint64_t last = adj_delay_us;

	// Adjust time with a PI controller
	const double kp = 0.5;
	const double ki = 0.25;
	int64_t proportional = (int64_t) time_int_us - (int64_t) elapsed_us;
	int64_t integral     = (int64_t) time_int_us * (interval + 1) - (int64_t) total_elapsed_us;
	adj_delay_us += kp * proportional;
	adj_delay_us += ki * integral;

	if (adj_delay_us < 0)
	{
		last *= 0;
		LOGINF("This interval ({}) was way too long. The next interval should last {} us. It will last {}."_format(interval, adj_delay_us, last));
		adj_delay_us = last;
	}
}


// Leave the machine in a consistent state
void clean(tasklist_t &tasklist, CAT_ptr_t cat, Perf &perf)
{
	LOGINF("Resetting CAT and performance counters...");
	cat->reset();
	perf.clean();

	// Try to drop privileges before killing anything
	LOGINF("Dropping privileges...");
	drop_privileges();

	LOGINF("Deleting run dirs...");
	try
	{
		for (const auto &task : tasklist)
			fs::remove_all(task->rundir);
	}
	catch(const std::exception &e)
	{
		const auto st = boost::get_error_info<traced>(e);
		if (st)
			LOGERR(e.what() << std::endl << *st);
		else
			LOGERR(e.what());
	}

	LOGINF("Killing children...");
	herod_the_great();
}


void clean_and_die(tasklist_t &tasklist, CAT_ptr_t cat, Perf &perf)
{
	LOGERR("--- PANIC, TRYING TO CLEAN ---");

	try
	{
		if (cat->is_initialized())
			cat->reset();
	}
	catch (const std::exception &e)
	{
		LOGERR("Could not reset CAT: " << e.what());
	}

	try
	{
		perf.clean();
	}
	catch (const std::exception &e)
	{
		LOGERR("Could not clean the performance counters: " << e.what());
	}

	// Kill children
	herod_the_great();

	LOGFAT("Exit with error");
}


std::string program_options_to_string(const std::vector<po::option>& raw)
{
	string args;

	for (const po::option& option: raw)
	{
		// if(option.unregistered) continue; // Skipping unknown options

		if(option.value.empty())
			args += option.string_key.size() == 1 ? "-" : "--" + option.string_key + "\n";
		else
		{
			// this loses order of positional options
			for (const std::string& value : option.value)
			{
				args += option.string_key.size() == 1 ? "-" : "--";
				args += option.string_key + ": ";
				args += value + "\n";
			}
		}
	}

	return args;
}


void open_output_streams(
		const string &int_str,
		const string &ucompl_str,
		const string &total_str,
		std::shared_ptr<std::ostream> &int_out,
		std::shared_ptr<std::ostream> &ucompl_out,
		std::shared_ptr<std::ostream> &total_out)
{
	// Open output file if needed; if not, use cout
	if (int_str == "")
	{
		int_out.reset(new std::ofstream());
		int_out->rdbuf(cout.rdbuf());
	}
	else
		int_out.reset(new std::ofstream(int_str));

	// Output file for summary stats until completion
	if (ucompl_str == "")
		ucompl_out.reset(new std::stringstream());
	else
		ucompl_out.reset(new std::ofstream(ucompl_str));

	// Output file for summary stats for all the time the applications have been executed, not only before they are completed
	if (total_str == "")
		total_out.reset(new std::stringstream());
	else
		total_out.reset(new std::ofstream(total_str));
}


// He is good with spoiled children
void herod_the_great()
{
	auto children = std::vector<pid_t>();
	pid_get_children_rec(getpid(), children);
	if (children.empty())
		return;

	LOGDEB("Herod the Gread has killed all the children he found: {}"_format(iterable_to_string(children.begin(), children.end(), [](const auto &t) {return "{}"_format(t);}, ", ")));
	for (const auto &child_pid : children)
		if (kill(child_pid, SIGKILL) < 0)
			LOGERR("Could not SIGKILL pid '{}', is he Jesus?: {}"_format(child_pid, strerror(errno)));
}


void sigint_handler(int signum)
{
	LOGWAR("-- SIGINT received --");
	//LOGWAR("Killing all child processes");
	//herod_the_great();
	longjmp (return_to_top_level,1);
	//exit(signum);
}

void sigabrt_handler(int signum)
{
	LOGWAR("-- SIGABRT received --");
    //LOGWAR("Killing all child processes");
    //herod_the_great();
	longjmp (return_to_top_level,1);
    //exit(signum);
}



int main(int argc, char *argv[])
{
	srand(time(NULL));
	signal(SIGINT, sigint_handler);
	signal(SIGABRT, sigabrt_handler);

	// Set the locale to the one defined in the corresponding enviroment variable
	std::setlocale(LC_ALL, "");

	// Default log conf
	const string min_clog = "inf";
	const string min_flog = "inf";

	// Options that can be setted using the config file
	CmdOptions options;

	po::options_description desc("Allowed options");
	desc.add_options()
		("help,h", "print usage message")
		("config,c", po::value<string>()->required(), "pathname for yaml config file")
		("config-override", po::value<string>()->default_value(""), "yaml string for overriding parts of the config")
		("output,o", po::value<string>()->default_value(""), "pathname for output")
		("fin-output", po::value<string>()->default_value(""), "pathname for output values when tasks are completed")
		("total-output", po::value<string>()->default_value(""), "pathname for total output values")
		("rundir", po::value<string>()->default_value("run"), "directory for creating the directories where the applications are gonna be executed")
		("id", po::value<string>()->default_value(random_string(10)), "identifier for the experiment")
		("ti", po::value<double>(), "time-interval, duration in seconds of the time interval to sample performance counters.")
		("mi", po::value<uint32_t>(), "max-intervals, maximum number of intervals.")
		("event,e", po::value<vector<string>>()->composing()->multitoken(), "optional list of custom events to monitor (up to 4)")
		("cpu-affinity", po::value<vector<uint32_t>>()->multitoken(), "cpus in which this application (not the workloads) is allowed to run")
		("clog-min", po::value<string>()->default_value(min_clog), "Minimum severity level to log into the console, defaults to warning")
		("flog-min", po::value<string>()->default_value(min_flog), "Minimum severity level to log into the log file, defaults to info")
		("log-file", po::value<string>()->default_value("manager.log"), "file used for the general application log")
		("cat-impl", po::value<string>(), "Which implementation of CAT to use (linux or intel)")
		;

	bool option_error = false;
	string option_str;
	po::variables_map vm;
	try
	{
		// Parse the options without storing them in a map.
		po::parsed_options parsed_options = po::command_line_parser(argc, argv)
			.options(desc)
			.run();
		option_str = program_options_to_string(parsed_options.options);

		po::store(parsed_options, vm);
		po::notify(vm);
	}
	catch(const std::exception &e)
	{
		cerr << e.what() << endl;
		option_error = true;
	}

	if (vm.count("help") || option_error)
	{
		cout << desc << endl;
		exit(option_error ? EXIT_FAILURE : EXIT_SUCCESS);
	}

	// Log init with user defined parameters
	general_log::init(
			vm["log-file"].as<string>(),
			general_log::severity_level(vm["clog-min"].as<string>()),
			general_log::severity_level(vm["flog-min"].as<string>()));

	// Log the program options
	string cmdline;
	for (int i = 0; i < argc; i++)
		cmdline += " {}"_format(argv[i]);
	LOGINF("Program cmdline:{}"_format(cmdline));
	LOGINF("Program options:\n" + option_str);

	// Open output streams
	auto int_out    = std::shared_ptr<std::ostream>();
	auto ucompl_out = std::shared_ptr<std::ostream>();
	auto total_out  = std::shared_ptr<std::ostream>();
	open_output_streams(vm["output"].as<string>(), vm["fin-output"].as<string>(), vm["total-output"].as<string>(), int_out, ucompl_out, total_out);

	// Read config
	auto tasklist = tasklist_t();
	auto coslist = vector<Cos>();
	CAT_ptr_t cat;
	sched::ptr_t sched;
	auto perf = Perf();
	auto catpol = std::make_shared<cat::policy::Base>(); // We want to use polimorfism, so we need a pointer
	string config_file;
	try
	{
		// Read config and set tasklist and coslist
		config_file = vm["config"].as<string>();
		string config_override = vm["config-override"].as<string>();
		config_read(config_file, config_override, options, tasklist, coslist, catpol, sched);
		tasks_set_rundirs(tasklist, vm["rundir"].as<string>() + "/" + vm["id"].as<string>());
	}
	catch(const YAML::ParserException &e)
	{
		LOGFAT(string("Error in config file in line: ") + to_string(e.mark.line) + " col: " + to_string(e.mark.column) + " pos: " + to_string(e.mark.pos) + ": " + e.msg);
	}
	catch(const std::exception &e)
	{
		const auto st = boost::get_error_info<traced>(e);
		if (st)
			LOGFAT("Error reading config file '" + config_file + "': " + e.what() << std::endl << *st) ;
		else
			LOGFAT("Error reading config file '" + config_file + "': " + e.what()) ;
	}

	// This options can be setted from the config file
	// The priority order is: commandline > config file > option defaults
	if (!vm["cat-impl"].empty())
		options.cat_impl = vm["cat-impl"].as<string>();
	if (!vm["ti"].empty())
		options.ti = vm["ti"].as<double>();
	if (!vm["mi"].empty())
		options.mi = vm["mi"].as<uint32_t>();
	if (!vm["event"].empty())
		options.event = vm["event"].as<vector<string>>();
	if (!vm["cpu-affinity"].empty())
		options.cpu_affinity = vm["cpu-affinity"].as<vector<uint32_t>>();

	// Set CPU affinity for not interfering with the executed workloads
	set_cpu_affinity(options.cpu_affinity);

	try
	{
		// Initial CAT configuration. It may be modified by the CAT policy.
		cat = cat_setup(options.cat_impl, coslist);
		catpol->set_cat(cat);
	}
	catch (const std::exception &e)
	{
		const auto st = boost::get_error_info<traced>(e);
		if (st)
			LOGFAT(e.what() << std::endl << *st);
		else
			LOGFAT(e.what());
	}

	try
	{
		// Execute and immediately pause tasks
		LOGINF("Launching and pausing tasks");
		for (const auto &task : tasklist)
			task_execute(*task);
		tasks_map_to_initial_clos(tasklist, std::dynamic_pointer_cast<CATLinux>(cat));
		LOGINF("Tasks ready");

		// Setup events
		for (const auto &task : tasklist)
			perf.setup_events(task->pid, options.event);

		// Start doing things
		LOGINF("Start main loop");
		if (setjmp(return_to_top_level) == 0)
			loop(tasklist, sched, catpol, perf, options.event, options.ti * 1000 * 1000, options.mi, *int_out, *ucompl_out, *total_out);
		else
			clean_and_die(tasklist, catpol->get_cat(), perf);
		// Leaving consistent state after throwing signal
		//int val = setjmp (return_to_top_level);
		//LOGWAR("val = {}"_format(val));
		//if (val)
		//	clean_and_die(tasklist, catpol->get_cat(), perf);


		// Kill tasks, reset CAT, performance monitors, etc...
		clean(tasklist, catpol->get_cat(), perf);

		// If no --fin-output argument, then the final stats are buffered in a stringstream and then outputted to stdout.
		// If we don't do this and the normal output also goes to stdout, they would mix.
		if (vm["fin-output"].as<string>() == "")
		{
			auto o = ucompl_out.get();
			cout << dynamic_cast<std::stringstream *>(o)->str();
		}
		// Same...
		if (vm["total-output"].as<string>() == "")
		{
			auto o = total_out.get();
			cout << dynamic_cast<std::stringstream *>(o)->str();
		}
	}
	catch(const std::exception &e)
	{
		const auto st = boost::get_error_info<traced>(e);
		if (st)
			LOGERR(e.what() << std::endl << *st);
		else
			LOGERR(e.what());
		clean_and_die(tasklist, catpol->get_cat(), perf);
	}
}
