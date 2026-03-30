#pragma once

/// @file Concurrency.h
/// @brief Umbrella header for the concurrency framework.
/// Include this single header to get access to all components.

#include "concurrency/Common.h"
#include "concurrency/queue/IQueue.h"
#include "concurrency/queue/MPMCQueue.h"
#include "concurrency/backpressure/BackpressurePolicy.h"
#include "concurrency/executor/IExecutor.h"
#include "concurrency/executor/Task.h"
#include "concurrency/executor/ThreadPoolExecutor.h"
#include "concurrency/event/Event.h"
#include "concurrency/event/IEventLoop.h"
#include "concurrency/event/EventLoop.h"
#include "concurrency/async/AsyncTask.h"
#include "concurrency/scheduler/Scheduler.h"
#include "concurrency/metrics/Metrics.h"
