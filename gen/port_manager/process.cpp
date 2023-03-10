#include <liim/try.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "process.h"

namespace PortManager {
Enviornment Enviornment::current() {
    auto enviornment = LIIM::Container::HashMap<String, String> {};
    for (auto** pair = environ; *pair; pair++) {
        auto key_equal_value = String(*pair);
        if (auto equal_index = key_equal_value.index_of('=')) {
            enviornment.try_emplace(key_equal_value.first(*equal_index), key_equal_value.substring(*equal_index + 1));
        }
    }
    return Enviornment(move(enviornment));
}

Result<Enviornment, Error> Enviornment::from_json(const JsonReader&, const Ext::Json::Object& object) {
    auto result = current();
    object.for_each([&](auto& key, auto& value) {
        result = move(result).set(key, Ext::Json::stringify(value));
    });
    return result;
}

Enviornment::Enviornment(LIIM::Container::HashMap<String, String> enviornment) : m_enviornment(move(enviornment)) {}

Enviornment Enviornment::set(String key, String value) && {
    m_enviornment.insert_or_assign(move(key), move(value));
    return Enviornment(move(m_enviornment));
}

auto Enviornment::get_c_style_envp() -> CStyleEnvp {
    auto storage = collect_vector(transform(m_enviornment, [](auto& pair) {
        return format("{}={}", tuple_get<0>(pair), tuple_get<1>(pair));
    }));

    auto envp = collect_vector(transform(storage, [](auto& value) {
        return value.string();
    }));
    envp.push_back(nullptr);

    return { move(storage), move(envp) };
}

Process Process::shell_command(String command) {
    return Process::command("sh", "-c", move(command));
}

Process Process::from_arguments(NewVector<String> arguments) {
    return Process(move(arguments), {});
}

Process::Process(NewVector<String> arguments, Option<Enviornment> enviornment)
    : m_arguments(move(arguments)), m_enviornment(move(enviornment)) {}

Process Process::with_enviornment(Enviornment enviornment) && {
    assert(!m_pid);
    return Process(move(m_arguments), move(enviornment));
}

Result<void, Error> Process::spawn_and_wait() {
    m_pid = TRY(spawn());
    return wait();
}

Result<pid_t, Error> Process::spawn() {
    NewVector<char*> arguments;
    for (auto& argument : m_arguments) {
        arguments.push_back(argument.string());
    }
    arguments.push_back(nullptr);

    pid_t pid;
    int result;
    if (m_enviornment) {
        auto c_enviornment = m_enviornment->get_c_style_envp();
        result = posix_spawnp(&pid, m_arguments[0].string(), nullptr, nullptr, arguments.data(), c_enviornment.envp.data());
    } else {
        result = posix_spawnp(&pid, m_arguments[0].string(), nullptr, nullptr, arguments.data(), environ);
    }
    if (result != 0) {
        return Err(make_string_error("Failed to spawn process \"{}\": {}", *this, strerror(result)));
    }
    return pid;
}

Result<void, Error> Process::wait() {
    assert(m_pid);
    int status;
    pid_t result = waitpid(*m_pid, &status, 0);
    if (result < 0) {
        return Err(make_string_error("Failed to wait on pid '{}': {}", *m_pid, strerror(errno)));
    } else if (result != *m_pid) {
        return Err(make_string_error("Waited on wrong pid: got {}, but expected {}", result, *m_pid));
    }

    if (WIFSIGNALED(status)) {
        return Err(make_string_error("Process \"{}\" was sent fatal signal: {}", *this, strsignal(WTERMSIG(status))));
    }
    assert(WIFEXITED(status));
    if (WEXITSTATUS(status) != 0) {
        return Err(make_string_error("Process \"{}\" terminated with non-zero exit code {}", *this, WEXITSTATUS(status)));
    }

    m_pid.reset();
    return {};
}

String Process::to_string() const {
    auto result = ""s;
    bool first = true;
    for (auto& argument : m_arguments) {
        if (!first) {
            result += " ";
        }
        result += argument;
        first = false;
    }
    return result;
}
}
