#pragma once

#include <cloudplay/core/session.hpp>
#include <mutex>
#include <ostream>

namespace cloudplay {

// Only typed lifecycle fields are accepted; credentials cannot be passed to this API.
// The output stream must outlive Logger. Each record is written under one lock.
class Logger {
  public:
    explicit Logger(std::ostream &output) : output_(output) {}
    void transition(const Transition &transition);

  private:
    std::ostream &output_;
    std::mutex mutex_;
};

} // namespace cloudplay
