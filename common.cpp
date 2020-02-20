#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <glib.h>
#include <fmt/format.h>
#include <grp.h>
#include "log.hpp"

#include "common.hpp"
#include "throw-with-trace.hpp"


namespace fs = boost::filesystem;

using fmt::literals::operator""_format;


// Opens an output stream and checks for errors
std::ofstream open_ofstream(const std::string &path)
{
	std::ofstream f;
	f.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	f.open(path);
	return f;
}


// Opens an intput stream and checks for errors
std::ifstream open_ifstream(const std::string &path)
{
	std::ifstream f;
	// Throw on error
	f.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	f.open(path);
	return f;
}


std::ifstream open_ifstream(const boost::filesystem::path &path)
{
	return open_ifstream(path.string());
}


std::ofstream open_ofstream(const boost::filesystem::path &path)
{
	return open_ofstream(path.string());
}


void assert_dir_exists(const boost::filesystem::path &dir)
{
	if (!fs::exists(dir))
		throw_with_trace(std::runtime_error("Dir {} does not exist"_format(dir.string())));
	if (!fs::is_directory(dir))
		throw_with_trace(std::runtime_error("{} is not a directory"_format(dir.string())));
}


// Returns the executable basename from a commandline
std::string extract_executable_name(const std::string &cmd)
{
	int argc;
	char **argv;

	if (!g_shell_parse_argv(cmd.c_str(), &argc, &argv, NULL))
		throw_with_trace(std::runtime_error("Could not parse commandline '" + cmd + "'"));

	std::string result = boost::filesystem::basename(argv[0]);
	g_strfreev(argv); // Free the memory allocated for argv

	return result;
}


void dir_copy(const std::string &source, const std::string &dest)
{
	namespace fs = boost::filesystem;

	if (!fs::exists(source) || !fs::is_directory(source))
		throw_with_trace(std::runtime_error("Source directory " + source + " does not exist or is not a directory"));
	if (fs::exists(dest))
		throw_with_trace(std::runtime_error("Destination directory " + dest + " already exists"));

	// Create dest
	if (!fs::create_directories(dest))
		throw_with_trace(std::runtime_error("Cannot create destination directory " + dest));

	dir_copy_contents(source, dest);
}


void dir_copy_contents(const std::string &source, const std::string &dest)
{
	namespace fs = boost::filesystem;

	if (!fs::exists(source) || !fs::is_directory(source))
		throw_with_trace(std::runtime_error("Source directory " + source + " does not exist or is not a directory"));
	if (!fs::exists(dest))
		throw_with_trace(std::runtime_error("Destination directory " + dest + " does not exist"));

	typedef fs::recursive_directory_iterator RDIter;
	for (auto it = RDIter(source), end = RDIter(); it != end; ++it)
	{
		const auto &path = it->path();
		auto relpath = it->path().string();
		boost::replace_first(relpath, source, ""); // Convert the path to a relative path

		fs::copy(path, dest + "/" + relpath);
	}
}


std::string random_string(size_t length)
{
	auto randchar = []() -> char
	{
		const char charset[] =
			"0123456789"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz";
		const size_t max_index = (sizeof(charset) - 1);
		return charset[rand() % max_index];
	};
	std::string str(length,0);
	std::generate_n(str.begin(), length, randchar);
	return str;
}


// Drop sudo privileges
void drop_privileges()
{
	const char *uidstr = getenv("SUDO_UID");
	const char *gidstr = getenv("SUDO_GID");
	const char *userstr = getenv("SUDO_USER");

	if (!uidstr || !gidstr || !userstr)
		return;

	const uid_t uid = std::stol(uidstr);
	const gid_t gid = std::stol(gidstr);

	if (uid == getuid() && gid == getgid())
		return;

	if (setgid(gid) < 0)
		throw_with_trace(std::runtime_error("Cannot change gid: " + std::string(strerror(errno))));

	if (initgroups(userstr, gid) < 0)
		throw_with_trace(std::runtime_error("Cannot change group access list: " + std::string(strerror(errno))));

	if (setuid(uid) < 0)
		throw_with_trace(std::runtime_error("Cannot change uid: " + std::string(strerror(errno))));
}


void set_cpu_affinity(std::vector<uint32_t> cpus, pid_t pid)
{
	// All cpus allowed
	if (cpus.size() == 0)
		return;

	// Set CPU affinity
	cpu_set_t mask;
	CPU_ZERO(&mask);
	for (auto cpu : cpus)
		CPU_SET(cpu, &mask);
	if (sched_setaffinity(pid, sizeof(mask), &mask) < 0)
		throw_with_trace(std::runtime_error("Could not set CPU affinity: " + std::string(strerror(errno))));
}
//#############################################################################################################
//Cambia la app con el pid indicado al cpu indicado
void set_cpu_affinity_V2(uint32_t cpu, pid_t pid)
{
        // All cpus allowed
        //if (cpus.size() == 0)
        //   return;

        // Set CPU affinity
         cpu_set_t mask;
         CPU_ZERO(&mask);
        // for (auto cpu : cpus)
             CPU_SET(cpu, &mask);
         if (sched_setaffinity(pid, sizeof(mask), &mask) < 0)
             throw_with_trace(std::runtime_error("Could not set CPU affinity: " + std::string(strerror(errno))));
}

//Busca el pid de la app que este en el mismo core fisico y haya conflicto
pid_t busca_tipo_y_core(std::vector<pairD_t> datos, pid_t pid_buscador)
{
    for(const auto &t : datos){

        if(std::get<0>(t) == pid_buscador){
            std::string type_actual = std::get<1>(t);
            //LOGINF("SOY EL BUSCADOR {}"_format(std::get<0>(t)));
            uint64_t cpu_actual = std::get<2>(t);
            //Busca apps con el mismo tipo y que no sea la que esta buscando conflictos
            for(const auto &y : datos)
            {
                /*
                LOGINF("---------------------------------------------");
                LOGINF("Type_Actual    :: {}"_format(type_actual));
                LOGINF("STD::GET<1>(Y) :: {}"_format(std::get<1>(y)));
                LOGINF("pid_buscador   :: {}"_format( pid_buscador));
                LOGINF("STD::GET<0>(y) :: {}"_format(std::get<0>(y)));
                LOGINF("---------------------------------------------");
                */

                //NO ES CONVENIENTE --> RET & RET || BB & BB , por ejemplo
                if(((type_actual == std::get<1>(y)) && std::get<0>(y) != pid_buscador && std::get<0>(y) > pid_buscador) && (type_actual == "retiring" || type_actual == "backend_bound") ) 
                {
                    //Si son del mismo core fisico debe devolver el pid de la app con la que hay conflicto
                    if( ((cpu_actual + 8) == std::get<2>(y)) || ((cpu_actual - 8) == std::get<2>(y)) )
                    {
                        pid_t a = std::get<0>(y); //App que crea conflicto
                        LOGINF("CONFLICTO >>  {} con el buscador  {} "_format(a,pid_buscador));
                        return a;
                    }
                }
            }
        }
    }
    //LOGINF("DEVUELVE -1");
    return -1;
}

//Busca una app que tenga tambien conflicto e intercambiar los cores
void cambia_core_valido(std::vector<pairD_t> datos, pid_t pid_cambia)
{
    uint32_t cpu_nuevo;
    for (const auto &t : datos)
    {
         if(std::get<0>(t) == pid_cambia)
        {
            uint32_t cpu_actual = std::get<2>(t);
            std::string type_actual = std::get<1>(t);
            //Busca apps que no sean de su core fisico y sin problemas
            for(const auto &y : datos )
            {
               //NO ES CONVENIENTE --> Del mismo tipo ( En este caso del mismo tipo )  
                if( ((cpu_actual + 8) != std::get<2>(y)) && ((cpu_actual - 8) != std::get<2>(y)) && (std::get<0>(y) != pid_cambia))
                {
             
                      //if( busca_tipo_y_core(datos, std::get<0>(y)) != -1 )
                      //{
                       
                        cpu_nuevo = std::get<2>(y);
                        //LOGINF("~~~~~ SI cambio");
                        //HAY QUE PONER ALGO AQUI PARA QUE NO SE PUEDAN PONER JUNTOS DOS APPS EN EL MISMO CORE FISICO
                        set_cpu_affinity_V2(cpu_nuevo, pid_cambia);
                        set_cpu_affinity_V2(cpu_actual,std::get<0>(y));
                        break; //Ya ha hecho el cambio de core
                     //}
                }
            }
        }
    }//si no hace break antes , no se moverá de lugar y ya está
}

//############################################################################################################


int get_self_cpu_id()
{
    /* Get the the current process' stat file from the proc filesystem */
	std::ifstream myReadFile;
	myReadFile.open("/proc/self/stat");
	char cNum[10];
	int i=0;
	int cpu_id = -1;
	bool found = false;

	while(!myReadFile.eof() & !found)
	{
		if (i < 38)
		{
			myReadFile.getline(cNum, 256, ' ');
			i = i + 1;
		}
		else
		{
			myReadFile.getline(cNum, 256, ' ');
			cpu_id = atoi(cNum);
			found = true;
		}
	}

    return cpu_id;
}

int get_cpu_id(pid_t pid)
{
    /* Get the the current process' stat file from the proc filesystem */
	std::ifstream myReadFile;
	myReadFile.open("/proc/{}/stat"_format(pid));
	char cNum[10];
	int i=0;
	int cpu_id = -1;
	bool found = false;

	while(!myReadFile.eof() & !found)
	{
		if (i < 38)
		{
			myReadFile.getline(cNum, 256, ' ');
			i = i + 1;
		}
		else
		{
			myReadFile.getline(cNum, 256, ' ');
			cpu_id = atoi(cNum);
			found = true;
		}
	}

    return cpu_id;
}


void pid_get_children_rec(const pid_t pid, std::vector<pid_t> &children)
{
	std::ifstream proc_children;
	proc_children.open("/proc/{}/task/{}/children"_format(pid, pid));
	pid_t child_pid = -1;
	while(proc_children >> child_pid)
	{
		children.push_back(child_pid);
		pid_get_children_rec(child_pid, children);
	}
}
