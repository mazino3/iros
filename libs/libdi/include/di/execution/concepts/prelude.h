#pragma once

#include <di/execution/concepts/awaitable.h>
#include <di/execution/concepts/awaiter.h>
#include <di/execution/concepts/completion_signature.h>
#include <di/execution/concepts/receiver.h>
#include <di/execution/concepts/receiver_of.h>
#include <di/execution/concepts/scheduler.h>
#include <di/execution/concepts/sender.h>
#include <di/execution/concepts/sender_of.h>
#include <di/execution/concepts/sender_to.h>
#include <di/execution/concepts/sends_stop.h>
#include <di/execution/concepts/valid_completion_signatures.h>

namespace di {
using concepts::Receiver;
using concepts::ReceiverOf;
using concepts::Scheduler;
using concepts::Sender;
using concepts::SenderOf;
using concepts::SenderTo;
using concepts::SendsStopped;
}