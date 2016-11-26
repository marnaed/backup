#pragma once

#include <unordered_map>
#include <vector>


class Point
{
	public:

	const size_t id;
	const std::vector<double> values;

	Point(size_t id, const std::vector<double>& values) :
			id(id), values(values) {}
};


class Cluster
{
	private:

	std::vector<double> centroid;
	std::unordered_map<int, const Point *> points;

	public:

	uint32_t id;

	Cluster(uint32_t id, const std::vector<double> &centroid) : centroid(centroid), id(id) {}

	void addPoint(const Point *point);
	void removePoint(int id_point);
	double distance(const Point &p) const;
	void updateMeans();
	const std::vector<double>& getCentroid() const { return centroid; }
	const std::unordered_map<int, const Point *>& getPoints() const { return points; }
	std::string to_string() const;
};


class KMeans
{
	private:

	// Return the index of the nearest cluster
	static
	int nearestCluster(size_t k, const std::vector<Cluster> &clusters, const Point &point);

	// Put an initial Point in them
	static
	void initClusters(size_t k, const std::vector<Point> &points, std::vector<Cluster> &clusters);

	public:

	// Clusterize
	static
	size_t clusterize(size_t k, const std::vector<Point> &points, std::vector<Cluster> &clusters, size_t max_iter);

	static
	std::string to_string(const std::vector<Cluster> &clusters);
};