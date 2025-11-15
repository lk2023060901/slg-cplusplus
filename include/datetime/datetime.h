#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "datetime/datetime_export.h"
#include "date/tz.h"

namespace slg::datetime {

class SLG_DATETIME_API DateTime {
public:
    using Clock = std::chrono::system_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    DateTime();
    explicit DateTime(std::chrono::milliseconds offset);

    TimePoint Now() const;
    void SetOffset(std::chrono::milliseconds offset) noexcept;
    std::chrono::milliseconds Offset() const noexcept;
    void AddSeconds(std::int64_t seconds) noexcept;
    void AddMinutes(std::int64_t minutes) noexcept;
    void AddHours(std::int64_t hours) noexcept;
    void AddDays(std::int64_t days) noexcept;
    void SetTimezone(std::string_view timezone);
    const date::time_zone* Timezone() const noexcept;
    const std::string& TimezoneName() const noexcept;

    static TimePoint Parse(std::string_view text,
                           std::string_view format,
                           std::string_view timezone);

    static std::string Format(const TimePoint& time_point,
                              std::string_view format,
                              std::string_view timezone);
    static std::string CurrentTimezoneName();
    static const date::time_zone* CurrentTimezone();

    static TimePoint FromUnixSeconds(std::int64_t seconds);
    static TimePoint FromUnixMilliseconds(std::int64_t milliseconds);
    static std::int64_t ToUnixSeconds(const TimePoint& time_point);
    static std::int64_t ToUnixMilliseconds(const TimePoint& time_point);
    static TimePoint Add(const TimePoint& time_point, std::chrono::milliseconds delta);
    static unsigned DaysInYear(int year);

    static bool IsLeapYear(int year);
    static unsigned DaysInMonth(int year, unsigned month);

    TimePoint StartOfToday(std::string_view timezone = {}) const;
    TimePoint StartOfYesterday(std::string_view timezone = {}) const;
    TimePoint StartOfTomorrow(std::string_view timezone = {}) const;
    TimePoint StartOfWeekday(int weekday,
                             std::string_view timezone = {},
                             int week_offset = 0) const;
    TimePoint StartOfSpecificDate(int year,
                                  unsigned month,
                                  unsigned day,
                                  std::string_view timezone = {}) const;

private:
    std::chrono::milliseconds offset_{0};
    std::string timezone_name_;
    const date::time_zone* timezone_{nullptr};
};

}  // namespace slg::datetime
