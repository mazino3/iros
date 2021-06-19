#include <errno.h>
#include <ext/file.h>
#include <ext/gzip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void print_usage_and_exit(const char* s) {
    fprintf(stderr, "Usage: %s <path>\n", s);
    exit(1);
}

int main(int argc, char** argv) {
    int opt;
    while ((opt = getopt(argc, argv, ":")) != -1) {
        switch (opt) {
            case ':':
            case '?':
                print_usage_and_exit(*argv);
                break;
        }
    }

    if (argc - optind != 1) {
        print_usage_and_exit(*argv);
    }

    String path = argv[optind];
    auto file = Ext::File::create(path, "r");
    if (!file) {
        fprintf(stderr, "decompress: failed to open file `%s': %s\n", path.string(), strerror(errno));
        return 1;
    }

    ByteWriter writer;
    Ext::GZipDecoder decoder(writer);

    auto stream_data = [&](const ByteBuffer& buffer) -> bool {
        auto result = decoder.stream_data(buffer.span());
        if (result == Ext::StreamResult::Error || result == Ext::StreamResult::NeedsMoreOutputSpace) {
            fprintf(stderr, "decompress: failed to decompress `%s'\n", path.string());
            return false;
        }

        if (result == Ext::StreamResult::Success) {
            return false;
        }

        return true;
    };

    ByteBuffer input_buffer(BUFSIZ);
    if (!file->read_all_streamed(input_buffer, move(stream_data))) {
        fprintf(stderr, "decompress: failed to read file `%s': %s\n", path.string(), strerror(file->error()));
        return 1;
    }

    fwrite(writer.data(), 1, writer.buffer_size(), stdout);
    return 0;
}
