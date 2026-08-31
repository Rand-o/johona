// imageworkers.hpp — dedicated thread pool for image decoding (and the
// schedule computation that feeds it).
//
// The decode sites used to run on QThreadPool::globalInstance() (32
// threads on a 16-core box).  Each 5K-8K JPEG decode holds a large
// transient buffer, so unbounded parallelism drove RSS peaks to
// 535-753 MiB (memory investigation).  A small pool bounds how many
// decode buffers can be live at once.

#pragma once

#include <QThreadPool>

namespace johona::gui {

inline QThreadPool* imageDecodePool() {
    static QThreadPool pool;
    pool.setMaxThreadCount(4);  // bounds parallel 5K-8K JPEG decode buffers
    return &pool;
}

}  // namespace johona::gui
