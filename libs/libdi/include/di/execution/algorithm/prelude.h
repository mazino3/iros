#pragma once

#include <di/execution/algorithm/into_variant.h>
#include <di/execution/algorithm/just.h>
#include <di/execution/algorithm/let.h>
#include <di/execution/algorithm/on.h>
#include <di/execution/algorithm/read.h>
#include <di/execution/algorithm/sync_wait.h>
#include <di/execution/algorithm/then.h>

namespace di {
using execution::sync_wait;
using execution::sync_wait_with_variant;
}