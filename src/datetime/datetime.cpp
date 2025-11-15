#include "datetime/datetime.h"

#include <sstream>
#include <stdexcept>
#include <string>

#include "date/date.h"
#include "date/tz.h"

namespace slg::datetime {
namespace {

constexpr std::string_view kDefaultTimezone{"UTC"};
constexpr std::string_view kDefaultFormat{"%Y-%m-%d %H:%M:%S"};

std::string ResolveSystemTimezone(std::string_view timezone) {
    if (!timezone.empty()) {
        return std::string(timezone);
    }
    try {
        auto current = date::current_zone();
        if (current != nullptr) {
            return current->name();
        }
    } catch (const std::runtime_error&) {
    }
    return std::string(kDefaultTimezone);
}

const date::time_zone* LocateZoneByName(const std::string& name) {
    try {
        return date::locate_zone(name);
    } catch (const std::runtime_error&) {
        throw std::invalid_argument("unknown time zone: " + name);
    }
}

const date::time_zone* LocateZone(std::string_view timezone) {
    auto resolved = ResolveSystemTimezone(timezone);
    return LocateZoneByName(resolved);
}

std::string PrepareFormat(std::string_view format) {
    if (format.empty()) {
        return std::string(kDefaultFormat);
    }
    return std::string(format);
}

DateTime::TimePoint LocalMidnightToSys(const date::time_zone* zone,
                                       date::local_time<std::chrono::seconds> local_time) {
    auto sys = zone->to_sys(local_time);
    return DateTime::TimePoint{
        std::chrono::duration_cast<DateTime::TimePoint::duration>(sys.time_since_epoch())};
}

DateTime::TimePoint StartOfDayImpl(const DateTime::TimePoint& reference,
                                   const date::time_zone* zone) {
    date::zoned_time<DateTime::TimePoint::duration> zoned(zone, reference);
    auto local_midnight = date::floor<date::days>(zoned.get_local_time());
    return LocalMidnightToSys(zone, date::local_time<std::chrono::seconds>(local_midnight));
}

int NormalizeWeekday(int weekday) {
    if (weekday < 1 || weekday > 7) {
        throw std::invalid_argument("weekday must be in [1, 7]");
    }
    return weekday % 7;
}

date::year_month_day MakeYearMonthDay(int year, unsigned month, unsigned day) {
    date::year_month_day ymd{date::year{year}, date::month{month}, date::day{day}};
    if (!ymd.ok()) {
        throw std::invalid_argument("invalid year/month/day");
    }
    return ymd;
}

}  // namespace

DateTime::DateTime() {
    timezone_name_ = ResolveSystemTimezone({});
    timezone_ = LocateZoneByName(timezone_name_);
}

DateTime::DateTime(std::chrono::milliseconds offset) : offset_(offset) {
    timezone_name_ = ResolveSystemTimezone({});
    timezone_ = LocateZoneByName(timezone_name_);
}

DateTime::TimePoint DateTime::Now() const {
    return Clock::now() + offset_;
}

void DateTime::SetOffset(std::chrono::milliseconds offset) noexcept {
    offset_ = offset;
}

std::chrono::milliseconds DateTime::Offset() const noexcept {
    return offset_;
}

void DateTime::AddSeconds(std::int64_t seconds) noexcept {
    offset_ += std::chrono::seconds(seconds);
}

void DateTime::AddMinutes(std::int64_t minutes) noexcept {
    offset_ += std::chrono::minutes(minutes);
}

void DateTime::AddHours(std::int64_t hours) noexcept {
    offset_ += std::chrono::hours(hours);
}

void DateTime::AddDays(std::int64_t days) noexcept {
    offset_ += std::chrono::hours(24 * days);
}

void DateTime::SetTimezone(std::string_view timezone) {
    std::string resolved = ResolveSystemTimezone(timezone);
    timezone_ = LocateZoneByName(resolved);
    timezone_name_ = std::move(resolved);
}

const date::time_zone* DateTime::Timezone() const noexcept {
    return timezone_;
}

const std::string& DateTime::TimezoneName() const noexcept {
    return timezone_name_;
}

DateTime::TimePoint DateTime::Parse(std::string_view text,
                                    std::string_view format,
                                    std::string_view timezone) {
    if (text.empty()) {
        throw std::invalid_argument("datetime string is empty");
    }

    auto zone = LocateZone(timezone);
    const auto fmt = PrepareFormat(format);

    std::istringstream iss{std::string(text)};
    iss.exceptions(std::ios::failbit | std::ios::badbit);
    date::local_time<TimePoint::duration> local_time;
    try {
        iss >> date::parse(fmt.c_str(), local_time);
    } catch (const std::ios_base::failure&) {
        throw std::invalid_argument("failed to parse datetime string: " + std::string(text));
    }

    return zone->to_sys(local_time);
}

std::string DateTime::Format(const TimePoint& time_point,
                             std::string_view format,
                             std::string_view timezone) {
    auto zone = LocateZone(timezone);
    const auto fmt = PrepareFormat(format);
    date::zoned_time<TimePoint::duration> zoned(zone, time_point);
    return date::format(fmt.c_str(), zoned);
}

std::string DateTime::CurrentTimezoneName() {
    return ResolveSystemTimezone({});
}

const date::time_zone* DateTime::CurrentTimezone() {
    return LocateZoneByName(ResolveSystemTimezone({}));
}

DateTime::TimePoint DateTime::FromUnixSeconds(std::int64_t seconds) {
    return TimePoint{std::chrono::seconds(seconds)};
}

DateTime::TimePoint DateTime::FromUnixMilliseconds(std::int64_t milliseconds) {
    return TimePoint{std::chrono::milliseconds(milliseconds)};
}

std::int64_t DateTime::ToUnixSeconds(const TimePoint& time_point) {
    return std::chrono::duration_cast<std::chrono::seconds>(
               time_point.time_since_epoch())
        .count();
}

std::int64_t DateTime::ToUnixMilliseconds(const TimePoint& time_point) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               time_point.time_since_epoch())
        .count();
}

DateTime::TimePoint DateTime::Add(const TimePoint& time_point, std::chrono::milliseconds delta) {
    return time_point + delta;
}

unsigned DateTime::DaysInYear(int year) {
    return date::year{year}.is_leap() ? 366u : 365u;
}

bool DateTime::IsLeapYear(int year) {
    return date::year{year}.is_leap();
}

unsigned DateTime::DaysInMonth(int year, unsigned month) {
    if (month < 1 || month > 12) {
        throw std::invalid_argument("month must be in [1, 12]");
    }
    date::year_month_day_last last{
        date::year{year}, date::month_day_last{date::month{month}}};
    auto ymd = date::year_month_day{last};
    return static_cast<unsigned>(ymd.day());
}

const date::time_zone* ResolveInstanceZone(const DateTime& dt, std::string_view timezone) {
    if (!timezone.empty()) {
        return LocateZone(timezone);
    }
    return dt.Timezone();
}

DateTime::TimePoint DateTime::StartOfToday(std::string_view timezone) const {
    return StartOfDayImpl(Now(), ResolveInstanceZone(*this, timezone));
}

DateTime::TimePoint DateTime::StartOfYesterday(std::string_view timezone) const {
    return StartOfDayImpl(Now() - std::chrono::hours(24), ResolveInstanceZone(*this, timezone));
}

DateTime::TimePoint DateTime::StartOfTomorrow(std::string_view timezone) const {
    return StartOfDayImpl(Now() + std::chrono::hours(24), ResolveInstanceZone(*this, timezone));
}

DateTime::TimePoint DateTime::StartOfWeekday(int weekday,
                                             std::string_view timezone,
                                             int week_offset) const {
    const auto zone = ResolveInstanceZone(*this, timezone);
    date::zoned_time<TimePoint::duration> zoned(zone, Now());
    auto local_time = zoned.get_local_time();
    auto local_days = date::floor<date::days>(local_time);
    date::weekday current(local_days);
    const int target = NormalizeWeekday(weekday);
    const int current_idx = current.c_encoding();
    int diff = target - current_idx + week_offset * 7;
    date::local_days target_days = local_days + date::days(diff);
    return LocalMidnightToSys(zone, date::local_time<std::chrono::seconds>(target_days));
}

DateTime::TimePoint DateTime::StartOfSpecificDate(int year,
                                                  unsigned month,
                                                  unsigned day,
                                                  std::string_view timezone) const {
    auto zone = ResolveInstanceZone(*this, timezone);
    auto ymd = MakeYearMonthDay(year, month, day);
    date::local_days local_day{ymd};
    return LocalMidnightToSys(zone, date::local_time<std::chrono::seconds>(local_day));
}

}  // namespace slg::datetime
