#include <iostream>

#include <sched.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <boost/filesystem.hpp>
#include <glib.h>

#include "task.hpp"


namespace fs = boost::filesystem;

using std::to_string;
using std::string;
using std::cerr;
using std::endl;


// Init static atribute
std::atomic<uint32_t> Task::ID(0);


void tasks_set_rundirs(std::vector<Task> &tasklist, const std::string &rundir_base)
{
	for (size_t i = 0; i < tasklist.size(); i++)
	{
		auto &task = tasklist[i];
		task.rundir = rundir_base + "/" + std::to_string(i) + "-" + task.executable;
		if (fs::exists(task.rundir))
			throw std::runtime_error("The rundir '" + task.rundir + "' already exists");
	}
}


void task_create_rundir(const Task &task)
{
	// Create rundir, either empty or with the files from the skel dir
	if (task.skel != "")
		dir_copy(task.skel, task.rundir);
	else
		if (!fs::create_directories(task.rundir))
			throw std::runtime_error("Could not create rundir directory " + task.rundir);

}


void task_remove_rundir(const Task &task)
{
	fs::remove_all(task.rundir);
}


// Pause task
void task_pause(const Task &task)
{
	int pid = task.pid;
	int status;

	if (pid <= 1)
		throw std::runtime_error("Tried to send SIGSTOP to pid " + to_string(pid) + ", check for bugs");

	kill(pid, SIGSTOP); // Stop child process
	waitpid(pid, &status, WUNTRACED); // Wait until it stops
	if (WIFEXITED(status))
		throw std::runtime_error("Command '" + task.cmd + "' with pid " + to_string(pid) + " exited unexpectedly with status " + to_string(WEXITSTATUS(status)));
}


// Pause multiple tasks
void tasks_pause(const std::vector<Task> &tasklist)
{
	for (const auto &task : tasklist)
		kill(task.pid, SIGSTOP); // Stop process

	for (const auto &task : tasklist)
	{
		int pid = task.pid;
		int status;

		if (pid <= 1)
			throw std::runtime_error("Tried to send SIGSTOP to pid " + to_string(pid) + ", check for bugs");

		waitpid(pid, &status, WUNTRACED); // Ensure it stopt
		if (WIFEXITED(status))
			throw std::runtime_error("Command '" + task.cmd + "' with pid " + to_string(pid) + " exited unexpectedly with status " + to_string(WEXITSTATUS(status)));
	}
}


// Resume multiple tasks
void tasks_resume(const std::vector<Task> &tasklist)
{
	for (const auto &task : tasklist)
		kill(task.pid, SIGCONT); // Resume process

	for (const auto &task : tasklist)
	{
		int pid = task.pid;
		int status;

		if (pid <= 1)
			throw std::runtime_error("Tried to send SIGCONT to pid " + to_string(pid) + ", check for bugs");

		waitpid(pid, &status, WCONTINUED); // Ensure it resumed
		if (WIFEXITED(status))
			throw std::runtime_error("Command '" + task.cmd + "' with pid " + to_string(pid) + " exited unexpectedly with status " + to_string(WEXITSTATUS(status)));
	}
}
// Execute a task and immediately pause it
void task_execute(Task &task)
{
	int argc;
	char **argv;

	if (!g_shell_parse_argv(task.cmd.c_str(), &argc, &argv, NULL))
		throw std::runtime_error("Could not parse commandline '" + task.cmd + "'");

	pid_t pid = fork();
	switch (pid) {
		// Child
		case 0:
		{
			// Set CPU affinity
			try
			{
				set_cpu_affinity({task.cpu});
			}
			catch (const std::exception &e)
			{
				cerr << "Error executing '" + task.cmd + "': " + e.what() << endl;
				exit(EXIT_FAILURE);
			}

			// Drop sudo privileges
			try
			{
				drop_privileges();
			}
			catch (const std::exception &e)
			{
				cerr << "Failed to drop privileges: " + string(e.what()) << endl;
			}

			// Create rundir with the necessary files and cd into it
			try
			{
				task_create_rundir(task);
			}
			catch (const std::exception &e)
			{
				cerr << "Could not create rundir: " + string(e.what()) << endl;
				exit(EXIT_FAILURE);
			}
			fs::current_path(task.rundir);

			// Redirect OUT/IN/ERR
			if (task.in != "")
			{
				fclose(stdin);
				if (fopen(task.in.c_str(), "r") == NULL)
				{
					cerr << "Failed to start program '" + task.cmd + "', could not open " + task.in << endl;
					exit(EXIT_FAILURE);
				}
			}
			if (task.out != "")
			{
				fclose(stdout);
				if (fopen(task.out.c_str(), "w") == NULL)
				{
					cerr << "Failed to start program '" + task.cmd + "', could not open " + task.out << endl;
					exit(EXIT_FAILURE);
				}
			}
			if (task.err != "")
			{
				fclose(stderr);
				if (fopen(task.err.c_str(), "w") == NULL)
				{
					cerr << "Failed to start program '" + task.cmd + "', could not open " + task.err << endl;
					exit(EXIT_FAILURE);
				}
			}

			// Exec
			execvp(argv[0], argv);

			// Should not reach this
			cerr << "Failed to start program '" + task.cmd + "'" << endl;
			exit(EXIT_FAILURE);
		}

			// Error
		case -1:
			throw std::runtime_error("Failed to start program '" + task.cmd + "'");

			// Parent
		default:
			usleep(100); // Wait a bit, just in case
			task.pid = pid;
			task_pause(task);
			g_strfreev(argv); // Free the memory allocated for argv
			break;
	}
}


void task_kill(Task &task)
{
	int pid = task.pid;
	if (pid > 1) // Never send kill to PID 0 or 1...
	{
		if (kill(pid, SIGKILL) < 0)
			throw std::runtime_error("Could not SIGKILL command '" + task.cmd + "' with pid " + to_string(pid) + ": " + strerror(errno));
		waitpid(pid, NULL, 0); // Wait until it exits...
		task.pid = 0;
	}
	else
	{
		throw std::runtime_error("Tried to kill pid " + to_string(pid) + ", check for bugs");
	}
}


// Kill and restart a task
void task_kill_and_restart(Task &task)
{
	task_kill(task);
	task.reset();
	task_remove_rundir(task);
	task_execute(task);
}


std::vector<uint32_t> tasks_cores_used(const std::vector<Task> &tasklist)
{
	auto res = std::vector<uint32_t>();
	for (const auto &task : tasklist)
		res.push_back(task.cpu);
	return res;
}


// Kill and restart the tasks that have reached their exec limit
void tasks_kill_and_restart(std::vector<Task> &tasklist)
{
	for (auto &task : tasklist)
		if (task.limit_reached)
			task_kill_and_restart(task);
}