#ifndef _COMMAND_H
#define _COMMAND_H 1

#include <liim/linked_list.h>
#include <liim/stack.h>
#include <liim/string.h>
#include <liim/vector.h>
#include <sh/sh_token.h>
#include <stdio.h>
#include <wordexp.h>

#define MAX_REDIRECTIONS 10

class PositionArgs {
public:
    Vector<char*> argv;
    int argc;

    PositionArgs(char** argv, int _argc) : argc(_argc) {
        for (int i = 0; i < argc; i++) {
            strings.add(String(argv[i]));
        }
        sync_argv();
    }

    const PositionArgs& operator=(const PositionArgs& other) {
        strings = other.strings;
        argc = other.argc;
        sync_argv();

        return *this;
    }

    void shift(int amount) {
        argv.clear();
        argc -= amount;

        auto first = strings.head();
        for (int i = 0; i <= amount; i++) {
            strings.remove(strings.head());
        }

        strings.prepend(first);
        sync_argv();
    }

    void add(char* s) {
        strings.add(String(s));
        argc++;
        sync_argv();
    }

    void prepend(char* s) {
        strings.prepend(String(s));
        argc++;
        sync_argv();
    }

    PositionArgs(const PositionArgs& other) : argc(other.argc), strings(other.strings) { sync_argv(); }

private:
    void sync_argv() {
        argv.clear();
        strings.for_each([&](const auto& s) {
            argv.add((char*) s.string());
        });
    }

    LinkedList<String> strings;
};

struct FunctionBody {
    ShValue::CompoundCommand compound_command;
    SharedPtr<String> for_posterity;
};

int command_run(ShValue::Program& program);

void command_init_special_vars(char* arg_zero);

void set_exit_status(int n);
int get_last_exit_status();

void command_push_position_params(const PositionArgs& args);
void command_pop_position_params();
size_t command_position_params_size();
char** command_position_params();
void command_shift_position_params_left(int amount);
void command_add_position_param(char* s);

void set_break_count(int count);
void set_continue_count(int count);
int get_loop_depth_count();

int get_exec_depth_count();
void inc_exec_depth_count();
void dec_exec_depth_count();
void set_should_return();
bool input_should_stop();

#endif /* _COMMAND_H */
