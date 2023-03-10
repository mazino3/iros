#include <cli/cli.h>
#include <liim/fixed_array.h>
#include <test/test.h>

struct Args {
    bool c { false };
    Option<int> i;
    StringView z;
    StringView dest;
};

constexpr auto cli_parser = [] {
    using namespace Cli;
    return Parser::of<Args>()
        .flag(Flag::boolean<&Args::c>().short_name('c').long_name("crash").description("Cause the program to crash"))
        .flag(Flag::optional<&Args::i>().short_name('i').long_name("int").description("The number of iterations"))
        .flag(Flag::required<&Args::z>().short_name('z').long_name("zip").description("Zip compression level"))
        .argument(Argument::single<&Args::dest>("dest").description("destination to destroy"));
}();

template<typename Parser>
static void do_cli_test(const Parser& parser, Span<StringView> input,
                        Function<void(typename Parser::OutputType&)> validate_result = nullptr) {
    auto result = parser.parse(input);
    if (!result) {
        auto message = format("{}", result.error());
        EXPECT_EQ(message, "Not an error");
        EXPECT(false);
    }

    validate_result.safe_call(result.value());
}

template<typename ErrorType, typename Parser>
static void do_cli_fail_test(const Parser& parser, Span<StringView> input, Function<void(ErrorType&)> validate_error = nullptr) {
    auto result = parser.parse(input);
    EXPECT(result.is_error());
    auto& error = result.error();

    if (!error.template is<ErrorType>()) {
        auto message = format("{}", result.error());
        EXPECT_EQ(message, "Different error type");
        EXPECT(false);
    }

    validate_error.safe_call(error.template as<ErrorType>());
}

static void do_basic_test(Span<StringView> input) {
    do_cli_test(cli_parser, input, [](auto& args) {
        EXPECT_EQ(args.c, true);
        EXPECT_EQ(args.dest, "file"sv);
        EXPECT_EQ(args.i, 42);
        EXPECT_EQ(args.z, "very");
    });
}

TEST(cli, short_flags) {
    FixedArray input = { "program"sv, "-c"sv, "-i42"sv, "-zvery"sv, "file"sv };
    do_basic_test(input.span());
}

TEST(cli, combined_short_flags) {
    FixedArray input = { "program"sv, "-ci42"sv, "file"sv, "-zvery"sv };
    do_cli_test(cli_parser, input.span(), [](auto& args) {
        EXPECT_EQ(args.c, true);
        EXPECT_EQ(args.dest, "file"sv);
        EXPECT_EQ(args.i, 42);
    });
}

TEST(cli, short_flags_separate_value) {
    FixedArray input = { "program"sv, "-i"sv, "42"sv, "-c"sv, "-zvery"sv, "file"sv };
    do_basic_test(input.span());
}

TEST(cli, long_flags) {
    FixedArray input = { "program"sv, "--int=42"sv, "--crash"sv, "--zip=very"sv, "file"sv };
    do_basic_test(input.span());
}

TEST(cli, long_flags_separate_value) {
    FixedArray input = { "program"sv, "--crash"sv, "--int", "42"sv, "file"sv, "--zip"sv, "very"sv };
    do_basic_test(input.span());
}

TEST(cli, mixed_flags) {
    FixedArray input = { "program"sv, "-i42"sv, "--crash", "file"sv, "--zip=very"sv };
    do_basic_test(input.span());
}

TEST(cli, no_flags) {
    FixedArray input { "prgram"sv, "file"sv, "-zvery" };
    do_cli_test(cli_parser, input.span(), [](auto& args) {
        EXPECT(!args.c);
        EXPECT(!args.i.has_value());
        EXPECT_EQ(args.z, "very");
    });
}

TEST(cli, missing_required_flag) {
    FixedArray input { "program"sv, "-i42"sv, "file"sv };
    do_cli_fail_test<Cli::MissingRequiredFlag>(cli_parser, input.span(), [](auto& error) {
        EXPECT_EQ(error.short_name(), 'z');
        EXPECT_EQ(error.long_name(), "zip");
    });
}

TEST(cli, missing_positional_argument) {
    FixedArray input { "program"sv, "-c"sv, "-zvery"sv };
    do_cli_fail_test<Cli::MissingPositionalArgument>(cli_parser, input.span(), [](auto& error) {
        EXPECT_EQ(error.argument_name(), "dest"sv);
    });
}

TEST(cli, extra_positional_argument) {
    FixedArray input { "program"sv, "-c"sv, "a"sv, "b"sv, "-zvery"sv };
    do_cli_fail_test<Cli::UnexpectedPositionalArgument>(cli_parser, input.span(), [](auto& error) {
        EXPECT_EQ(error.value(), "b"sv);
    });
}

TEST(cli, missing_short_flag_value) {
    FixedArray input { "program"sv, "file"sv, "-zvery"sv, "-i"sv };
    do_cli_fail_test<Cli::ShortFlagRequiresValue>(cli_parser, input.span(), [](auto& error) {
        EXPECT_EQ(error.flag(), 'i');
    });
}

TEST(cli, missing_long_flag_value) {
    FixedArray input { "program"sv, "file"sv, "-zvery"sv, "--int"sv };
    do_cli_fail_test<Cli::LongFlagRequiresValue>(cli_parser, input.span(), [](auto& error) {
        EXPECT_EQ(error.flag(), "int"sv);
    });
}

TEST(cli, extra_short_flag) {
    FixedArray input { "program"sv, "-x"sv, "file"sv, "-zvery"sv };
    do_cli_fail_test<Cli::UnexpectedShortFlag>(cli_parser, input.span(), [](auto& error) {
        EXPECT_EQ(error.flag(), 'x');
    });
}

TEST(cli, extra_long_flag) {
    FixedArray input { "program"sv, "-zvery"sv, "--xxx"sv, "file"sv };
    do_cli_fail_test<Cli::UnexpectedLongFlag>(cli_parser, input.span(), [](auto& error) {
        EXPECT_EQ(error.flag(), "xxx"sv);
    });
}

struct Args2 {
    bool c { false };
    Option<int> i;
    StringView source;
    Vector<StringView> dest;
};

constexpr auto right_parser = [] {
    using namespace Cli;
    return Parser::of<Args2>()
        .flag(Flag::boolean<&Args2::c>().short_name('c').long_name("crash").description("Cause the program to crash"))
        .flag(Flag::optional<&Args2::i>().short_name('i').long_name("int").description("The number of iterations"))
        .argument(Argument::single<&Args2::source>("source").description("source to destroy"))
        .argument(Argument::list<&Args2::dest>("dest").description("destination to destroy"));
}();

constexpr auto left_parser = [] {
    using namespace Cli;
    return Parser::of<Args2>()
        .flag(Flag::boolean<&Args2::c>().short_name('c').long_name("crash").description("Cause the program to crash"))
        .flag(Flag::optional<&Args2::i>().short_name('i').long_name("int").description("The number of iterations"))
        .argument(Argument::list<&Args2::dest>("dest").description("destination to destroy"))
        .argument(Argument::single<&Args2::source>("source").description("source to destroy"));
}();

TEST(cli, variable_right_position_arguments) {
    FixedArray input = { "program"sv, "-ci42"sv, "ss"sv, "xx"sv, "yy"sv, "zz"sv };
    do_cli_test(right_parser, input.span(), [](auto& args) {
        EXPECT_EQ(args.c, true);
        EXPECT_EQ(args.i, 42);
        EXPECT_EQ(args.source, "ss"sv);
        EXPECT_EQ(args.dest.size(), 3);
        EXPECT_EQ(args.dest[0], "xx"sv);
        EXPECT_EQ(args.dest[1], "yy"sv);
        EXPECT_EQ(args.dest[2], "zz"sv);
    });
}

TEST(cli, variable_left_position_arguments) {
    FixedArray input = { "program"sv, "-ci42"sv, "xx"sv, "yy"sv, "zz"sv, "ss"sv };
    do_cli_test(left_parser, input.span(), [](auto& args) {
        EXPECT_EQ(args.c, true);
        EXPECT_EQ(args.i, 42);
        EXPECT_EQ(args.source, "ss"sv);
        EXPECT_EQ(args.dest.size(), 3);
        EXPECT_EQ(args.dest[0], "xx"sv);
        EXPECT_EQ(args.dest[1], "yy"sv);
        EXPECT_EQ(args.dest[2], "zz"sv);
    });
}

TEST(cli, empty_right_positional_argument_list) {
    FixedArray input = { "program"sv, "-ci42"sv, "ss"sv };
    do_cli_fail_test<Cli::EmptyPositionalArgumentList>(right_parser, input.span(), [](auto& error) {
        EXPECT_EQ(error.argument_name(), "dest"sv);
    });
}

TEST(cli, empty_left_positional_argument_list) {
    FixedArray input = { "program"sv, "-ci42"sv, "ss"sv };
    do_cli_fail_test<Cli::EmptyPositionalArgumentList>(left_parser, input.span(), [](auto& error) {
        EXPECT_EQ(error.argument_name(), "dest"sv);
    });
}

struct Args3 {
    int i { 42 };
};

constexpr auto defaulted_parser = [] {
    return Cli::Parser::of<Args3>().flag(Cli::Flag::defaulted<&Args3::i>().short_name('i'));
}();

TEST(cli, defaulted_value) {
    FixedArray input = { "program"sv };
    do_cli_test(defaulted_parser, input.span(), [](auto& args) {
        EXPECT_EQ(args.i, 42);
    });
}

TEST(cli, defaulted_with_value) {
    FixedArray input = { "program"sv, "-i"sv, "32"sv };
    do_cli_test(defaulted_parser, input.span(), [](auto& args) {
        EXPECT_EQ(args.i, 32);
    });
}
