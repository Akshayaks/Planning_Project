/* Copyright (C) Jiaoyang Li
* Unauthorized copying of this file, via any medium is strictly prohibited
* Confidential
* Written by Jiaoyang Li <jiaoyanl@usc.edu>, May 2020
*/

/*driver.cpp
* Solve a MAPF instance on 2D grids.
*/
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include "ECBS.h"


/* Main function */
int main(int argc, char** argv)
{
	namespace po = boost::program_options;
	// Declare the supported options.
	po::options_description desc("Allowed options");
	desc.add_options()
		("help", "produce help message")

		// params for the input instance and experiment settings
		("map,m", po::value<string>()->required(), "input file for map")
		("agents,a", po::value<string>()->required(), "input file for agents")
		("output,o", po::value<string>(), "output file for statistics")
		("outputPaths", po::value<string>(), "output file for paths")
		("agentNum,k", po::value<int>()->default_value(0), "number of agents")
		("cutoffTime,t", po::value<double>()->default_value(7200), "cutoff time (seconds)")
		("screen,s", po::value<int>()->default_value(1), "screen option (0: none; 1: results; 2:all)")
		("stats", po::value<bool>()->default_value(false), "write to files some detailed statistics")

		// params for CBS node selection strategies
		("highLevelSolver", po::value<string>()->default_value("EES"), "the high-level solver (A*, A*eps, EES, NEW)")
		("lowLevelSolver", po::value<bool>()->default_value(true), "using suboptimal solver in the low level")
		("inadmissibleH", po::value<string>()->default_value("Global"), "inadmissible heuristics (Zero, Global, Path, Local, Conflict)")
		("suboptimality", po::value<double>()->default_value(1.2), "suboptimality bound")

		// params for CBS improvement
		("heuristics", po::value<string>()->default_value("WDG"), "admissible heuristics for the high-level search (Zero, CG,DG, WDG)")
		("prioritizingConflicts", po::value<bool>()->default_value(true), "conflict prioirtization. If true, conflictSelection is used as a tie-breaking rule.")
		("bypass", po::value<bool>()->default_value(true), "Bypass1")
		("disjointSplitting", po::value<bool>()->default_value(false), "disjoint splitting")
		("rectangleReasoning", po::value<bool>()->default_value(false), "rectangle reasoning")
		("corridorReasoning", po::value<bool>()->default_value(true), "corridor reasoning")
		("targetReasoning", po::value<bool>()->default_value(true), "target reasoning")
		("sipp", po::value<bool>()->default_value(0), "using SIPPS as the low-level solver")
		("restart", po::value<int>()->default_value(0), "rapid random restart times")
		;
	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);

	if (vm.count("help")) {
		cout << desc << endl;
		return 1;
	}

	po::notify(vm);
	if (vm["suboptimality"].as<double>() < 1)
	{
		cerr << "Suboptimal bound should be at least 1!" << endl;
		return -1;
	}

	high_level_solver_type s;
	if (vm["highLevelSolver"].as<string>() == "A*")
		s = high_level_solver_type::ASTAR;
	else if (vm["highLevelSolver"].as<string>() == "A*eps")
		s = high_level_solver_type::ASTAREPS;
	else if (vm["highLevelSolver"].as<string>() == "EES")
		s = high_level_solver_type::EES;
	else if (vm["highLevelSolver"].as<string>() == "NEW")
		s = high_level_solver_type::NEW;
	else
	{
		cout << "WRONG high level solver!" << endl;
		return -1;
	}

	if (s == high_level_solver_type::ASTAR && vm["suboptimality"].as<double>() > 1)
	{
		cerr << "A* cannot perform suboptimal search!" << endl;
		return -1;
	}

    heuristics_type h;
	if (vm["heuristics"].as<string>() == "Zero")
		h = heuristics_type::ZERO;
	else if (vm["heuristics"].as<string>() == "CG")
		h = heuristics_type::CG;
	else if (vm["heuristics"].as<string>() == "DG")
		h = heuristics_type::DG;
	else if (vm["heuristics"].as<string>() == "WDG")
		h = heuristics_type::WDG;
	else
	{
		cout << "WRONG heuristics strategy!" << endl;
		return -1;
	}

    if ((h == heuristics_type::CG || h == heuristics_type::DG) && vm["lowLevelSolver"].as<bool>())
    {
        cerr << "CG or DG heuristics do not work with low level of suboptimal search!" << endl;
        return -1;
    }

	heuristics_type h_hat; // inadmissible heuristics
	if (s == high_level_solver_type::ASTAR ||
	    s == high_level_solver_type::ASTAREPS ||
	    vm["inadmissibleH"].as<string>() == "Zero")
		h_hat = heuristics_type::ZERO;
	else if (vm["inadmissibleH"].as<string>() == "Global")
		h_hat = heuristics_type::GLOBAL;
	else if (vm["inadmissibleH"].as<string>() == "Path")
		h_hat = heuristics_type::PATH;
	else if (vm["inadmissibleH"].as<string>() == "Local")
		h_hat = heuristics_type::LOCAL;
	else if (vm["inadmissibleH"].as<string>() == "Conflict")
		h_hat = heuristics_type::CONFLICT;
	else
	{
		cout << "WRONG inadmissible heuristics strategy!" << endl;
		return -1;
	}

	conflict_selection conflict = conflict_selection::EARLIEST;
	node_selection n = node_selection::NODE_CONFLICTPAIRS;


	srand((int)time(0));

	///////////////////////////////////////////////////////////////////////////
	// load the instance
	Instance instance(vm["map"].as<string>(), vm["agents"].as<string>(),
		vm["agentNum"].as<int>()); //While creating the map, the obstacles are being added at random!
	cout << "Number of agents: " << vm["agentNum"].as<int>() <<endl;

	srand(0);
	int runs = 1 + vm["restart"].as<int>();
	//////////////////////////////////////////////////////////////////////
    // initialize the solver
	if (vm["lowLevelSolver"].as<bool>())
    {
        int success_trial = 0;
		vector<float> all_runtimes;
		for(int trial = 1; trial < 2; trial++){


			ECBS ecbs(instance, vm["sipp"].as<bool>(), vm["screen"].as<int>());
			ecbs.setPrioritizeConflicts(vm["prioritizingConflicts"].as<bool>());
			ecbs.setDisjointSplitting(vm["disjointSplitting"].as<bool>());
			ecbs.setBypass(vm["bypass"].as<bool>());
			ecbs.setRectangleReasoning(vm["rectangleReasoning"].as<bool>());
			ecbs.setCorridorReasoning(vm["corridorReasoning"].as<bool>());
			ecbs.setHeuristicType(h, h_hat);
			ecbs.setTargetReasoning(vm["targetReasoning"].as<bool>());
			ecbs.setMutexReasoning(false);
			ecbs.setConflictSelectionRule(conflict);
			ecbs.setNodeSelectionRule(n);
			ecbs.setSavingStats(vm["stats"].as<bool>());
			ecbs.setHighLevelSolver(s, vm["suboptimality"].as<double>());
			//////////////////////////////////////////////////////////////////////
			// run
			double runtime = 0;
			int lowerbound = 0;
			for (int i = 0; i < runs; i++)
			{
				ecbs.clear();
				ecbs.solve(vm["cutoffTime"].as<double>() / runs, lowerbound);
				runtime += ecbs.runtime;
				if (ecbs.solution_found)
					break;
				lowerbound = ecbs.getLowerBound(); //max(current lowerbound, min f value in cleanup list)
				ecbs.randomRoot = true;
				cout << "Failed to find solutions in Run " << i << endl;
			}
			ecbs.runtime = runtime;
			all_runtimes.push_back(runtime);
			if(runtime < 60){
				success_trial++;
			}
			if (vm.count("output"))
				ecbs.saveResults(vm["output"].as<string>(), vm["agents"].as<string>());
			if (ecbs.solution_found && vm.count("outputPaths"))
				ecbs.savePaths(vm["outputPaths"].as<string>());
			/*size_t pos = vm["output"].as<string>().rfind('.');      // position of the file extension
			string output_name = vm["output"].as<string>().substr(0, pos);     // get the name without extension
			cbs.saveCT(output_name); // for debug*/
			if (vm["stats"].as<bool>())
				ecbs.saveStats(vm["output"].as<string>(), vm["agents"].as<string>());
			ecbs.clearSearchEngines();
		}
		cout << "Success rate: " << success_trial << endl;
		cout << "Runtime: ";
		for(int k = 0;k < all_runtimes.size(); k++){
			cout << all_runtimes[k] << " " << endl;
		}
    }
    else
    {
        int success_trial = 0;
		vector<float> all_runtimes;
		for(int trial = 1; trial < 26; trial++){
			///////////////////////////////////////////////////////////////////////////
			string scene_file = "scen-random-32-4/room-32-32-4-random-"+std::to_string(trial)+"scen";
			// load the instance
			Instance instance(vm["map"].as<string>(), scene_file,
				vm["agentNum"].as<int>()); //While creating the map, the obstacles are being added at random!

			srand(0);
			int runs = 1 + vm["restart"].as<int>();
			//////////////////////////////////////////////////////////////////////
			CBS cbs(instance, vm["sipp"].as<bool>(), vm["screen"].as<int>());
			cbs.setPrioritizeConflicts(vm["prioritizingConflicts"].as<bool>());
			cbs.setDisjointSplitting(vm["disjointSplitting"].as<bool>());
			cbs.setBypass(vm["bypass"].as<bool>());
			cbs.setRectangleReasoning(vm["rectangleReasoning"].as<bool>());
			cbs.setCorridorReasoning(vm["corridorReasoning"].as<bool>());
			cbs.setHeuristicType(h, h_hat);
			cbs.setTargetReasoning(vm["targetReasoning"].as<bool>());
			cbs.setMutexReasoning(false);
			cbs.setConflictSelectionRule(conflict);
			cbs.setNodeSelectionRule(n);
			cbs.setSavingStats(vm["stats"].as<bool>());
			cbs.setHighLevelSolver(s, vm["suboptimality"].as<double>());
			//////////////////////////////////////////////////////////////////////
			// run
			double runtime = 0;
			int lowerbound = 0;
			for (int i = 0; i < runs; i++)
			{
				cbs.clear();
				cbs.solve(vm["cutoffTime"].as<double>() / runs, lowerbound);
				runtime += cbs.runtime;
				if (cbs.solution_found)
				{
					cout << "\n****************Solution Found**********" << endl;
					break;
				}
				lowerbound = cbs.getLowerBound();
				cbs.randomRoot = true;
				cout << "Failed to find solutions in Run " << i << endl;
			}
			cbs.runtime = runtime;
			all_runtimes.push_back(runtime);
			if(runtime < 60){
				success_trial++;
			}
			cout << "\nCBS runtime: " << runtime << endl;
			if (vm.count("output"))
				cbs.saveResults(vm["output"].as<string>(), vm["agents"].as<string>());
			if (cbs.solution_found && vm.count("outputPaths"))
				cbs.savePaths(vm["outputPaths"].as<string>());
			if (vm["stats"].as<bool>())
				cbs.saveStats(vm["output"].as<string>(), vm["agents"].as<string>());
			cout << "\nWrote output";
			cbs.clearSearchEngines();
			cout << "\nCleared";
		}
		cout << "\n*********Done with lyf****\n";
		cout << "Success rate: " << success_trial << endl;
		for(int k = 0;k < all_runtimes.size(); k++){
			cout << all_runtimes[k] << " ";
		}
	}
	return 0;

}