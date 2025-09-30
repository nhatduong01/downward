//
// Created by Duong Nguyen on 28.09.25.
//

#ifndef FAST_DOWNWARD_PLAN_PARSER_H
#define FAST_DOWNWARD_PLAN_PARSER_H

#include <string>
#include <vector>

using namespace std;

struct PlanAction {
    string name;
    vector <string> arguments;
};

struct ParsedPlan {
    vector<PlanAction> actions;
    std::optional<int> cost;

    void print_plan() const;
};

class PlanParser {
    string filename;
    static string trim(const string &s);
    public:
    explicit PlanParser(const string &path);
    ParsedPlan parse();
};
#endif // FAST_DOWNWARD_PLAN_PARSER_H
