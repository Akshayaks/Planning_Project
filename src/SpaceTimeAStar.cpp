#include "SpaceTimeAStar.h"


void SpaceTimeAStar::updatePath(const LLNode* goal, vector<PathEntry> &path)
{
    const LLNode* curr = goal;
    cout << "\nBacktracking from: " << curr->theta << endl;
    if (curr->is_goal)
        curr = curr->parent;
    path.reserve(curr->g_val + 1);
    while (curr != nullptr)
    {
        // cout << "\nIn path: " << curr->theta;
        PathEntry p(curr->location,curr->theta);
        p.location = curr->location;
        p.theta = curr->theta;

        // cout << "\nCreated: " << p->theta <<endl;
        path.emplace_back(p);
        curr = curr->parent;
    }
    std::reverse(path.begin(),path.end());

    // cout << "\nPath found by backtracking: " << endl;
    // for(auto p:path){
    //     cout << "\n" << p.location << ":" << p.theta;
    // }
}


Path SpaceTimeAStar::findOptimalPath(const HLNode& node, const ConstraintTable& initial_constraints,
                                     const vector<Path*>& paths, int agent, int lowerbound)
{
    return findSuboptimalPath(node, initial_constraints, paths, agent, lowerbound, 1).first;
}

//Low level planner for each agent
// find path by time-space A* search
// Returns a bounded-suboptimal path that satisfies the constraints of the give node  while
// minimizing the number of internal conflicts (that is conflicts with known_paths for other agents found so far).
// lowerbound is an underestimation of the length of the path in order to speed up the search.
pair<Path, int> SpaceTimeAStar::findSuboptimalPath(const HLNode& node, const ConstraintTable& initial_constraints,
                                                   const vector<Path*>& paths, int agent, int lowerbound, double w)
{
    //low level lowerbound initially 0
    this->w = w;
    Path path;
    num_expanded = 0;
    num_generated = 0;

    // build constraint table
    auto t = clock();
    ConstraintTable constraint_table(initial_constraints); //constraints empty for root node
    constraint_table.insert2CT(node, agent); //inserts all constraints at its parents to the current node
    runtime_build_CT = (double)(clock() - t) / CLOCKS_PER_SEC;
    if (constraint_table.constrained(start_location, 0)) // Check if at the beg (t=0) start location is constrained
    {
        cout << "\nStart location is constrained";
        return {path, 0};
    }

    t = clock();
    //What is collision avoidance table: a dynamic lookup table: stores the location and time of every agent in 
    // every group. Then, when an MAPF solver is applied for a given group, ties between nodes with
// the same f -value are broken in favor of the node that has fewer entries in the CAT.
    constraint_table.insert2CAT(agent, paths); //initially path is empty
    runtime_build_CAT = (double)(clock() - t) / CLOCKS_PER_SEC;

    // if(agent == 0){
    //     start_location = 0;
    //     goal_location = 13;
    // }
    // else{
    //     start_location = 24;
    //     goal_location = 12;
    // }

    cout << "\nAgent number: " << agent << endl;
    cout << "Start loc: " << start_location/instance.num_of_cols << " " << start_location % instance.num_of_cols << endl;
    cout << "Goal loc: " << goal_location/instance.num_of_cols << " " << goal_location % instance.num_of_cols << endl;

    

    // the earliest timestep that the agent can hold its goal location. The length_min is considered here.
    auto holding_time = constraint_table.getHoldingTime(goal_location, constraint_table.length_min);
    auto static_timestep = constraint_table.getMaxTimestep() + 1; // everything is static after this timestep
    lowerbound =  max(holding_time, lowerbound);

    // generate start and add it to the OPEN & FOCAL list
    // AStarNode* start;
    // if(agent == 1){
    //     start = new AStarNode(start_location, 90, 0, max(lowerbound, my_heuristic[start_location]), nullptr, 0, 0);
    // }
    // else{
    //     start = new AStarNode(start_location, 0, 0, max(lowerbound, my_heuristic[start_location]), nullptr, 0, 0);
    // }
    auto start = new AStarNode(start_location, 0, 0, max(lowerbound, my_heuristic[start_location]), nullptr, 0, 0);

    num_generated++;
    start->open_handle = open_list.push(start);
    start->focal_handle = focal_list.push(start);
    start->in_openlist = true;
    allNodes_table.insert(start);
    min_f_val = (int) start->getFVal();
    // lower_bound = int(w * min_f_val));

    while (!open_list.empty())
    {
        updateFocalList(); // update FOCAL if min f-val increased
        auto* curr = popNode(); // pop from Focal and remove that node from open
        assert(curr->location >= 0);
        // check if the popped node is a goal
        if (curr->location == goal_location && // arrive at the goal location
            !curr->wait_at_goal && // not wait at the goal location
            curr->timestep >= holding_time) // the agent can hold the goal location afterward
        {
            updatePath(curr, path); //Backtrack
            cout << "\nFound the goal in single agent planning";
            break;
        }

        if (curr->timestep >= constraint_table.length_max)
            continue;

        list<list<pair<int, double> > > primitives;
        if(!curr->in_progress)
        {
            // cout << "new primitives" << endl;
            primitives = instance.getPrimitives(curr->location, curr->theta);
        }
        else // long primitive in progress, there is only one neighbor - the next cell in the path
        {
            primitives.push_back(list<pair<int, double> >{curr->path_remaining.front()});
            curr->path_remaining.pop_front();
        }
        // primitives.emplace_back(make_pair(curr->location,curr->theta)); // Add current location also as the agent can wait there?
        pair<int, double> next_location;
        for (auto next_neighbor : primitives)
        {
            // cout << next_neighbor.size() << endl;
            next_location = next_neighbor.front();
            // cout << "theta: " << next_location.second << endl;

            int next_timestep = curr->timestep + 1;
            if (static_timestep < next_timestep) //Do not understand this part
            { // now everything is static, so switch to space A* where we always use the same timestep
                if (next_location.first == curr->location)
                {
                    continue;
                }
                next_timestep--;
            }

            if (constraint_table.constrained(next_location.first, next_timestep) ||
                constraint_table.constrained(curr->location, next_location.first, next_timestep)) // vertex and edge collision why check here?
                continue;

            // compute cost to next_id via curr node
            int next_g_val = curr->g_val + 1;
            // int next_h_val = my_heuristic[next_location.first];
            int next_h_val = max(lowerbound - next_g_val, my_heuristic[next_location.first]);
            // cout << "\nh: " << next_h_val;
            if (next_g_val + next_h_val > constraint_table.length_max)
                continue;
            int next_internal_conflicts = curr->num_of_conflicts +
                                          constraint_table.getNumOfConflictsForStep(curr->location, next_location.first, next_timestep);

            // generate (maybe temporary) node
            auto next = new AStarNode(next_location.first, next_location.second, next_g_val, next_h_val,
                                      curr, next_timestep, next_internal_conflicts);

            if(next_neighbor.size() > 1) // new long primitive neighbor initialize
            {
                next->in_progress = true;
                next->path_remaining = next_neighbor;
                next->path_remaining.pop_front(); // 'next' is already the first cell in path (next_location) - remove it
            }
            else if(curr->in_progress and !curr->path_remaining.empty()) // path already in progress
            {
                // cout << "in progress" << endl;
                next->in_progress = true;
                next->path_remaining = curr->path_remaining;
            }

            if (next_location.first == goal_location && curr->location == goal_location)
                next->wait_at_goal = true;

            // try to retrieve it from the hash table
            // check if node already in closed list? it is an element in CBS class
            auto it = allNodes_table.find(next);
            if (it == allNodes_table.end())
            {
                pushNode(next); //push into open and focal list
                // cout << "\nPushing: " << next->theta <<endl;
                allNodes_table.insert(next);
                continue;
            }
            // update existing node's if needed (only in the open_list)

            auto existing_next = *it; //it now points to the HL node?
            if (existing_next->getFVal() > next->getFVal() || // if f-val decreased through this new path
                (existing_next->getFVal() == next->getFVal() &&
                 existing_next->num_of_conflicts > next->num_of_conflicts)) // or it remains the same but there's fewer conflicts
            {
                if (!existing_next->in_openlist) // if it is in the closed list (reopen)
                {
                    existing_next->copy(*next);
                    // cout << "pushing: " << existing_next->theta << endl;
                    pushNode(existing_next); //Add it back to open and focal
                }
                else
                {
                    bool add_to_focal = false;  // check if it was above the focal bound before and now below (thus need to be inserted)
                    bool update_in_focal = false;  // check if it was inside the focal and needs to be updated (because f-val changed)
                    bool update_open = false;
                    if ((next_g_val + next_h_val) <= w * min_f_val)
                    {  // if the new f-val qualify to be in FOCAL
                        if (existing_next->getFVal() > w * min_f_val)
                            add_to_focal = true;  // and the previous f-val did not qualify to be in FOCAL then add
                        else
                            update_in_focal = true;  // and the previous f-val did qualify to be in FOCAL then update
                    }
                    if (existing_next->getFVal() > next_g_val + next_h_val)
                        update_open = true;

                    existing_next->copy(*next);	// update existing node

                    if (update_open)
                        open_list.increase(existing_next->open_handle);  // increase because f-val improved
                    if (add_to_focal)
                        existing_next->focal_handle = focal_list.push(existing_next);
                    if (update_in_focal)
                        focal_list.update(existing_next->focal_handle);  // should we do update? yes, because number of conflicts may go up or down
                }
            }

            delete(next);  // not needed anymore -- we already generated it before
        }  // end for loop that generates successors
    }  // end while loop

    releaseNodes();
    cout << "\nFinal paths found : ";
    for(auto p:path){
        cout << "\n" << p.location << ":" << p.theta << endl;
    }
    return {path, min_f_val};
}


int SpaceTimeAStar::getTravelTime(int start, int end, const ConstraintTable& constraint_table, int upper_bound)
{
    int length = MAX_TIMESTEP;
    auto static_timestep = constraint_table.getMaxTimestep() + 1; // everything is static after this timestep
    auto root = new AStarNode(start, 0, compute_heuristic(start, end), nullptr, 0, 0);
    root->open_handle = open_list.push(root);  // add root to heap
    allNodes_table.insert(root);       // add root to hash_table (nodes)
    AStarNode* curr = nullptr;
    while (!open_list.empty())
    {
        curr = open_list.top(); open_list.pop();
        if (curr->location == end)
        {
            length = curr->g_val;
            break;
        }
        auto next_locations = instance.getPrimitives(curr->location,curr->theta);
        // next_locations.emplace_back(curr->location);
        for (auto next_l : next_locations)
        {
            pair<int,double> next_location = next_l.front();
            int next_timestep = curr->timestep + 1;
            int next_g_val = curr->g_val + 1;
            if (static_timestep < next_timestep)
            {
                if (curr->location == next_location.first)
                {
                    continue;
                }
                next_timestep--;
            }
            if (!constraint_table.constrained(next_location.first, next_timestep) &&
                !constraint_table.constrained(curr->location, next_location.first, next_timestep))
            {  // if that grid is not blocked
                int next_h_val = compute_heuristic(next_location.first, end);
                if (next_g_val + next_h_val >= upper_bound) // the cost of the path is larger than the upper bound
                    continue;
                auto next = new AStarNode(next_location.first, next_location.second, next_g_val, next_h_val, nullptr, next_timestep, 0);
                auto it = allNodes_table.find(next);
                if (it == allNodes_table.end())
                {  // add the newly generated node to heap and hash table
                    next->open_handle = open_list.push(next);
                    allNodes_table.insert(next);
                }
                else {  // update existing node's g_val if needed (only in the heap)
                    delete(next);  // not needed anymore -- we already generated it before
                    auto existing_next = *it;
                    if (existing_next->g_val > next_g_val)
                    {
                        existing_next->g_val = next_g_val;
                        existing_next->timestep = next_timestep;
                        open_list.increase(existing_next->open_handle);
                    }
                }
            }
        }
    }
    releaseNodes();
    return length;
}

inline AStarNode* SpaceTimeAStar::popNode()
{
    auto node = focal_list.top(); focal_list.pop();
    open_list.erase(node->open_handle);
    node->in_openlist = false;
    num_expanded++;
    return node;
}


inline void SpaceTimeAStar::pushNode(AStarNode* node)
{
    node->open_handle = open_list.push(node);
    node->in_openlist = true;
    num_generated++;
    if (node->getFVal() <= w * min_f_val)
        node->focal_handle = focal_list.push(node);
}


void SpaceTimeAStar::updateFocalList()
{
    auto open_head = open_list.top();
    if (open_head->getFVal() > min_f_val)
    {
        int new_min_f_val = (int)open_head->getFVal();
        for (auto n : open_list)
        {
            if (n->getFVal() >  w * min_f_val && n->getFVal() <= w * new_min_f_val)
                n->focal_handle = focal_list.push(n);
        }
        min_f_val = new_min_f_val;
    }
}


void SpaceTimeAStar::releaseNodes()
{
    open_list.clear();
    focal_list.clear();
    for (auto node: allNodes_table)
        delete node;
    allNodes_table.clear();
}
