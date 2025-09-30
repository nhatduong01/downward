//
// Created by Duong Nguyen on 28.09.25.
//

#include "plan_parser.h"

#include <iostream>
#include <fstream>
#include <sstream>


void ParsedPlan::print_plan() const {
    for (const auto &a : actions) {
        cout << a.name << "(";
        for (const auto & arg: a.arguments) {
            cout << arg <<" ";
        }
        cout << ")" << endl;
    }
}
string PlanAction::to_string() const {
    string result = this->name;
    for (const auto &a : this->arguments) {
        result += " " + a;
    }
    return result;
}
string PlanParser::trim(const string &s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

 PlanParser::PlanParser(const string &path): filename(path) {}

ParsedPlan PlanParser::parse() {
    ParsedPlan plan;
    std::ifstream in(this->filename);
    if (!in) {
        throw runtime_error("Could not open file " + this->filename);
    }
    string line;
    int line_num = 0;
    while (std::getline(in, line)) {
        ++line_num;
        string curr_line = trim(line);
        if (curr_line.empty()) continue;
        if (curr_line[0] == ';') {
            string tag = "; cost =";
            if (auto pos = curr_line.find(tag); pos != std::string::npos) {
                string num = curr_line.substr(pos + tag.size());
                plan.cost = stoi(num);
            }
            continue;
        }

        if (curr_line.front() != '(' || curr_line.back() != ')') {
            throw std::runtime_error("Invalid format at line" + std::to_string(line_num));
        }
        curr_line = curr_line.substr(1, curr_line.size() - 2);
        std::istringstream ss(curr_line);

        PlanAction action;
        ss >> action.name;
        string arg;
        while (ss >> arg) {
           action.arguments.push_back(arg);
        }

        plan.actions.push_back(action);
    }
    return plan;
}