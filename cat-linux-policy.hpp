#pragma once

#include "cat-policy.hpp"
#include "cat-linux.hpp"


#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/rolling_window.hpp>
#include <boost/accumulators/statistics/rolling_variance.hpp>
#include <set>
#include <deque>

namespace cat
{


namespace policy
{

namespace acc = boost::accumulators;


class LinuxBase : public Base
{
      public:

      LinuxBase() = default;
      virtual ~LinuxBase() = default;

      // Safely cast CAT to LinuxCat
      std::shared_ptr<CATLinux> get_cat()
      {
          auto ptr = std::dynamic_pointer_cast<CATLinux>(cat);
          if (ptr)
              return ptr;
          else
              throw_with_trace(std::runtime_error("Linux CAT implementation required"));
      }

      // Derived classes should perform their operations here. This base class does nothing by default.
      virtual void apply(uint64_t, const tasklist_t &) {}
};


// No partition policy
class NoPart: public Base
{
    protected:
	std::shared_ptr<CAT> catLinux = std::make_shared<CATLinux>();
    uint64_t every = -1;
    std::string stats = "total";

	double expected_IPC = 0;

    public:
    virtual ~NoPart() = default;
    NoPart(uint64_t _every, std::string _stats) : every(_every), stats(_stats){}
    virtual void apply(uint64_t, const tasklist_t &) override;
};
typedef NoPart NP;


class CriticalAware: public LinuxBase
{

    protected:
    uint64_t every = -1;
    uint64_t firstInterval = 1;

    //Masks of CLOS
    uint64_t maskCrCLOS = 0xfffff;
    uint64_t num_ways_CLOS_2 = 20;
    uint64_t maskNonCrCLOS = 0xfffff;
    uint64_t num_ways_CLOS_1 = 20;

    int64_t num_shared_ways = 0;

    //Control of the changes made in the masks
    uint64_t state = 0;
    double expectedIPCtotal = 0;
    double ipc_CR_prev = 0;
    double ipc_NCR_prev = 0;

    double mpkiL3Mean = 0;
	double stdmpkiL3Mean = 0;

    bool firstTime = true;

    uint64_t IDLE_INTERVALS = 5;
    uint64_t idle_count = IDLE_INTERVALS;
    bool idle = false;

	// Define accumulators
	typedef acc::accumulator_set<
		double, acc::stats<
			acc::tag::rolling_mean,
			acc::tag::rolling_variance
		>
	>
	ca_accum_t;

	ca_accum_t macc;

    //vector to store if task is assigned to critical CLOS
	typedef std::tuple<pid_t, uint64_t> pair_t;
    std::vector<pair_t> taskIsInCRCLOS;
	std::vector<pair_t> pid_CPU;

	// number of times a task has been critical
	std::map<pid_t,uint64_t> frequencyCritical;

    public:

	//typedef std::tuple<pid_t, uint64_t> pair_t

    CriticalAware(uint64_t _every, uint64_t _firstInterval) : every(_every), firstInterval(_firstInterval), macc(acc::tag::rolling_window::window_size = 10u) {}

    virtual ~CriticalAware() = default;

    //configure CAT
    void reset_configuration(const tasklist_t &);

	// calculate median of vector of tuples
	typedef std::tuple<pid_t, double> pairD_t;
	double medianV(std::vector<pairD_t> &vec);

	virtual void apply(uint64_t current_interval, const tasklist_t &tasklist);

};
typedef CriticalAware CA;


class CriticalAwareV2: public LinuxBase
{
	protected:
    uint64_t every = -1;
    uint64_t firstInterval = 1;

    // Masks of CLOS
    uint64_t maskCrCLOS = 0xfffff;
    uint64_t num_ways_CLOS_2 = 20;
    uint64_t maskNonCrCLOS = 0xfffff;
    uint64_t num_ways_CLOS_1 = 20;
	int64_t num_shared_ways = 0;
	uint64_t prev_critical_apps = 0;

    // Control of the changes made in the masks
    uint64_t state = 0;
    double expectedIPCtotal = 0;
    double ipc_CR_prev = 0;
    double ipc_NCR_prev = 0;

	// Flags
    bool firstTime = true;
	bool idle = false;

	// IDLE control variables
    uint64_t IDLE_INTERVALS = 5;
    uint64_t idle_count = IDLE_INTERVALS;

    // vector to store if task is assigned to critical CLOS
    typedef std::tuple<pid_t, uint64_t> pair_t;
	typedef std::tuple<pid_t, double> pairD_t;
	//std::set<double> all_mpkil3;
    std::vector<pair_t> taskIsInCRCLOS;
    std::vector<pair_t> pid_CPU;
	std::vector<pairD_t> v_mpkil3_prev;

	// number of times a task has been critical
    std::map<pid_t,uint64_t> frequencyCritical;

    // dictionary with CLOSes and corresponding masks
	// { 5, 0x0000C }
    std::map<uint64_t, uint64_t> clos_mask = {
        { 3, 0x00003 }
    };

    uint64_t CLOS_key = 3;

	//dictinary holding previous value of critical apps
	std::map<pid_t, double> ipc_critical_prev;
	bool false_critical_app = false;
	pid_t excluded_application;
	double excluded_application_ipc;

	// dictionary holding deque with 3 last MPKIL3 values for each task
	std::map<pid_t, std::deque<double>> deque_mpkil3;
	// dictionary holding up to 10 last MPKIL3 valid (non-spike) values
	std::map<pid_t, std::deque<double>> valid_mpkil3;

	// number of times consecutive critical_apps = 0 detected
	uint64_t num_no_critical = 0;

    public:

	CriticalAwareV2(uint64_t _every, uint64_t _firstInterval) : every(_every), firstInterval(_firstInterval) {}

    virtual ~CriticalAwareV2() = default;

	void reset_configuration(const tasklist_t &);

    //configure CAT
    void update_configuration(std::vector<pair_t> v, std::vector<pair_t> status, uint64_t num_critical_old, uint64_t num_critical_new);

    virtual void apply(uint64_t current_interval, const tasklist_t &tasklist);

};
typedef CriticalAwareV2 CAV2;


//-----------------------------------------------------------------------------
// Aux functions
//-----------------------------------------------------------------------------


void tasks_to_closes(catlinux_ptr_t cat, const tasklist_t &tasklist, const clusters_t &clusters);
std::string cluster_to_tasks(const Cluster &cluster, const tasklist_t &tasklist);


class ClusteringBase: public LinuxBase
{
	public:

	class CouldNotCluster : public std::runtime_error
	{
		public:
		CouldNotCluster(const std::string &msg) : std::runtime_error(msg) {};
	};

	ClusteringBase() = default;
	virtual ~ClusteringBase() = default;

	virtual clusters_t apply(const tasklist_t &tasklist);

};
typedef std::shared_ptr<ClusteringBase> ClusteringBase_ptr_t;


class Cluster_SF: public ClusteringBase
{
	public:

	int m;
	std::vector<int> sizes;

	Cluster_SF(const std::vector<int> &_sizes) : sizes(_sizes) {}
	virtual ~Cluster_SF() = default;

	virtual clusters_t apply(const tasklist_t &tasklist) override;
};


class Cluster_KMeans: public ClusteringBase
{
	public:

	int num_clusters;
	int max_clusters;
	EvalClusters eval_clusters;
	std::string event;
	bool sort_ascending;

	Cluster_KMeans(int _num_clusters, int _max_clusters, EvalClusters _eval_clusters, std::string _event, bool _sort_ascending) :
			num_clusters(_num_clusters), max_clusters(_max_clusters),
			eval_clusters(_eval_clusters), event(_event),
			sort_ascending(_sort_ascending) {}
	virtual ~Cluster_KMeans() = default;

	virtual clusters_t apply(const tasklist_t &tasklist) override;
};


class DistributingBase
{
	public:

	int min_ways;
	int max_ways;

	DistributingBase()
	{
		auto info = cat_read_info();
		min_ways = info["L3"].min_cbm_bits;
		max_ways = __builtin_popcount(info["L3"].cbm_mask);
	}

	DistributingBase(int _min_ways, int _max_ways) : min_ways(_min_ways), max_ways(_max_ways) {}

	uint32_t cut_mask(uint32_t mask)
	{
		return mask & ~((uint32_t) -1 << max_ways);
	}

	virtual ~DistributingBase() = default;

	virtual cbms_t apply(const tasklist_t &, const clusters_t &) { return cbms_t(); };
};
typedef std::shared_ptr<DistributingBase> DistributingBase_ptr_t;


class Distribute_N: public DistributingBase
{
	int n;

	public:

	Distribute_N(int _n): n(_n) {}
	Distribute_N(int _min_ways, int _max_ways, int _n): DistributingBase(_min_ways, _max_ways), n(_n) {}
	virtual ~Distribute_N() = default;

	virtual cbms_t apply(const tasklist_t &, const clusters_t &clusters) override;
};


class Distribute_Static: public DistributingBase
{
	cbms_t masks;

	public:

	Distribute_Static(const cbms_t &_masks) : masks(_masks){}
	virtual ~Distribute_Static() = default;

	virtual cbms_t apply(const tasklist_t &, const clusters_t &) override { return masks; }
};


class Distribute_RelFunc: public DistributingBase
{
	bool invert_metric;

	public:

	Distribute_RelFunc() = default;
	Distribute_RelFunc(bool _invert_metric) : invert_metric(_invert_metric) {};
	Distribute_RelFunc(int _min_ways, int _max_ways, bool _invert_metric) :
			DistributingBase(_min_ways, _max_ways), invert_metric(_invert_metric) {};
	virtual ~Distribute_RelFunc() = default;

	virtual cbms_t apply(const tasklist_t &, const clusters_t &clusters) override;
};


class SquareWave: public LinuxBase
{
	bool is_down;

	public:

	class Wave
	{
		public:
		bool is_down;
		uint32_t interval;
		cbm_t up;
		cbm_t down;
		Wave() = default;
		Wave(uint32_t _interval, cbm_t _up, cbm_t _down) : is_down(false), interval(_interval), up(_up), down(_down) {}
	};

	std::vector<Wave> waves;
	ClusteringBase clustering;

	SquareWave() = delete;
	SquareWave(std::vector<Wave> _waves) : waves(_waves), clustering() {}

	virtual void apply(uint64_t current_interval, const tasklist_t &tasklist);
};


class ClusterAndDistribute: public LinuxBase
{

	uint32_t every;
	ClusteringBase_ptr_t clustering;
	DistributingBase_ptr_t distributing;

	public:

	ClusterAndDistribute() = delete;
	ClusterAndDistribute(uint32_t _every, ClusteringBase_ptr_t _clustering, DistributingBase_ptr_t _distributing) :
			every(_every), clustering(_clustering), distributing(_distributing) {}

	virtual void apply(uint64_t current_interval, const tasklist_t &tasklist);
	void show(const tasklist_t &tasklist, const clusters_t &clusters, const cbms_t &ways);
};

}} // cat::policy
