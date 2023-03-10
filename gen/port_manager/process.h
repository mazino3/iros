#pragma once

#include <ext/json.h>
#include <liim/container/hash_map.h>
#include <liim/container/new_vector.h>
#include <liim/format.h>
#include <liim/result.h>
#include <liim/string.h>

#include "error.h"

namespace PortManager {
class Enviornment {
public:
    static Enviornment current();
    static Result<Enviornment, Error> from_json(const JsonReader& reader, const Ext::Json::Object& object);

    Enviornment clone() const { return Enviornment(m_enviornment.clone()); }

    Enviornment set(String key, String value) &&;

    struct CStyleEnvp {
        NewVector<String> storage;
        NewVector<char*> envp;
    };
    CStyleEnvp get_c_style_envp();

private:
    explicit Enviornment(LIIM::Container::HashMap<String, String> enviornment);

    LIIM::Container::HashMap<String, String> m_enviornment;
};

class Process {
public:
    template<typename... Args>
    static Process command(Args&&... args) {
        NewVector<String> arguments;
        (arguments.push_back(format("{}", forward<Args>(args))), ...);
        return Process(move(arguments), None {});
    }
    static Process shell_command(String command);
    static Process from_arguments(NewVector<String> arguments);

    Process with_enviornment(Enviornment enviornment) &&;

    Result<void, Error> spawn_and_wait();
    Result<pid_t, Error> spawn();
    Result<void, Error> wait();

    String to_string() const;

private:
    explicit Process(NewVector<String> arguments, Option<Enviornment> enviornment);

    NewVector<String> m_arguments;
    Option<Enviornment> m_enviornment;
    Option<pid_t> m_pid;
};
}
