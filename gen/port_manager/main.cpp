#include <cli/cli.h>
#include <ext/json.h>
#include <ext/system.h>
#include <liim/result.h>
#include <liim/try.h>

#include "config.h"
#include "context.h"
#include "error.h"
#include "port.h"
#include "step.h"

namespace PortManager {
struct Arguments {
    Path json_path { "port.json" };
    StringView build_step { "install" };
};

constexpr auto argument_parser = [] {
    using namespace Cli;
    return Parser::of<Arguments>()
        .flag(Flag::defaulted<&Arguments::json_path>().short_name('f').long_name("port-json").description("path to port json file"))
        .argument(Argument::defaulted<&Arguments::build_step>("step").description("build step to perform"));
}();

Result<void, Error> main(Arguments arguments) {
    auto context = Context(TRY(Config::create()));

    auto path = TRY(Ext::System::realpath(arguments.json_path).transform_error([&](auto error) {
        return make_string_error("Failed to resolve json path `{}': {}", arguments.json_path, error.message());
    }));

    auto handle = TRY(context.load_port(move(path)));
    return context.build_port(handle, arguments.build_step);
}
}

CLI_MAIN(PortManager::main, PortManager::argument_parser)
