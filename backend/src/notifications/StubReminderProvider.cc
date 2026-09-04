#include "StubReminderProvider.h"

namespace notifications
{
void StubReminderProvider::sendReminder(const std::string &userId,
                                         const std::string &pinkyPromiseId,
                                         const std::string &activityText,
                                         std::chrono::system_clock::time_point eventTime)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sent_.push_back(SentReminder{userId, pinkyPromiseId, activityText, eventTime});
}

std::vector<SentReminder> StubReminderProvider::sent() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return sent_;
}

void StubReminderProvider::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    sent_.clear();
}

}  // namespace notifications
