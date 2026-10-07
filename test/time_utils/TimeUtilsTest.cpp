#include <Arduino.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "HalClock.h"
#include "TimeUtils.h"
#include "Timezones.h"

namespace {

uint32_t utcEpoch(int year, unsigned month, unsigned day, unsigned hour, unsigned minute) {
  return TimeUtils::getDayOrdinalForDate(year, month, day) * 86400u + hour * 3600u + minute * 60u;
}

struct ClockTestState {
  const bool hadTz = getenv("TZ") != nullptr;
  const std::string savedTz = hadTz ? getenv("TZ") : "";
  const CrossPointSettings savedSettings = SETTINGS;
  const time_t savedNow = halClock.now;

  ClockTestState() {
    SETTINGS = CrossPointSettings{};
    halClock.now = 0;
    timezones::applyToClock();
  }
  ~ClockTestState() {
    SETTINGS = savedSettings;
    halClock.now = savedNow;
    if (hadTz)
      setenv("TZ", savedTz.c_str(), 1);
    else
      unsetenv("TZ");
    tzset();
  }
};

uint8_t zoneIndex(const char* name) {
  for (size_t i = 0; i < timezones::count(); ++i) {
    if (strcmp(timezones::table()[i].name, name) == 0) return static_cast<uint8_t>(i);
  }
  ADD_FAILURE() << "Missing timezone: " << name;
  return timezones::utcIndex();
}

struct LocalTimeCase {
  uint8_t offsetQ;
  uint32_t epoch;
  int year;
  int month;
  int day;
  int hour;
  int minute;
};

}  // namespace

TEST(TimeUtils, FixedOffsetConversionIgnoresProcessTimezone) {
  ClockTestState state;
  halClock.setTimezone("EST5EDT,M3.2.0,M11.1.0");

  const LocalTimeCase cases[] = {
      {0, utcEpoch(2025, 1, 2, 10, 30), 2025, 1, 1, 22, 30}, {104, utcEpoch(2025, 1, 2, 10, 30), 2025, 1, 3, 0, 30},
      {71, utcEpoch(2025, 1, 2, 18, 30), 2025, 1, 3, 0, 15}, {49, utcEpoch(2025, 1, 2, 23, 50), 2025, 1, 3, 0, 5},
      {47, utcEpoch(2025, 1, 3, 0, 5), 2025, 1, 2, 23, 50},  {255, utcEpoch(2025, 1, 2, 10, 30), 2025, 1, 2, 10, 30},
  };

  for (const auto& test : cases) {
    std::tm local{};
    EXPECT_TRUE(TimeUtils::getLocalDateTime(test.epoch, test.offsetQ, local));
    EXPECT_EQ(local.tm_year + 1900, test.year);
    EXPECT_EQ(local.tm_mon + 1, test.month);
    EXPECT_EQ(local.tm_mday, test.day);
    EXPECT_EQ(local.tm_hour, test.hour);
    EXPECT_EQ(local.tm_min, test.minute);
  }
}

TEST(TimeUtils, LocalDateTimeRoundTripsAcrossOffsets) {
  ClockTestState state;
  const char* zones[] = {"UTC12", "UTC0:15", "UTC0", "UTC-0:15", "UTC-5:45", "UTC-14"};
  for (const char* zone : zones) {
    halClock.setTimezone(zone);
    uint32_t epoch = 0;
    ASSERT_TRUE(TimeUtils::localDateTimeToUtcEpoch(2024, 2, 29, 23, 45, epoch));

    std::tm local{};
    ASSERT_TRUE(TimeUtils::getLocalDateTime(epoch, local));
    EXPECT_EQ(local.tm_year + 1900, 2024);
    EXPECT_EQ(local.tm_mon + 1, 2);
    EXPECT_EQ(local.tm_mday, 29);
    EXPECT_EQ(local.tm_hour, 23);
    EXPECT_EQ(local.tm_min, 45);
  }
}

TEST(TimeUtils, ManualDateValidationHandlesLeapYearsAndBounds) {
  ClockTestState state;
  uint32_t epoch = 0;
  EXPECT_TRUE(TimeUtils::localDateTimeToUtcEpoch(2024, 2, 29, 0, 0, epoch));
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2025, 2, 29, 0, 0, epoch));
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2023, 12, 31, 23, 59, epoch));
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2100, 1, 1, 0, 0, epoch));
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2025, 13, 1, 0, 0, epoch));
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2025, 1, 1, 24, 0, epoch));
  EXPECT_EQ(TimeUtils::getDaysInMonth(2024, 2), 29u);
  EXPECT_EQ(TimeUtils::getDaysInMonth(2025, 2), 28u);
}

TEST(TimeUtils, FormatsTwelveAndTwentyFourHourClock) {
  const uint32_t midnight = utcEpoch(2025, 6, 1, 0, 5);
  const uint32_t afternoon = utcEpoch(2025, 6, 1, 13, 7);
  char buffer[16];

  ASSERT_TRUE(TimeUtils::formatTime(midnight, 48, false, buffer, sizeof(buffer)));
  EXPECT_STREQ(buffer, "00:05");
  ASSERT_TRUE(TimeUtils::formatTime(midnight, 48, true, buffer, sizeof(buffer)));
  EXPECT_STREQ(buffer, "12:05 AM");
  ASSERT_TRUE(TimeUtils::formatTime(afternoon, 48, true, buffer, sizeof(buffer)));
  EXPECT_STREQ(buffer, "1:07 PM");
}

TEST(TimeUtils, LegacyOffsetMigrationRemainsFixedUntilCityIsSelected) {
  ClockTestState state;
  const uint8_t offsets[] = {0, 34, 48, 71, 80, 104};
  const uint32_t now = utcEpoch(2025, 7, 2, 3, 15);
  for (const uint8_t offset : offsets) {
    SETTINGS.clockUtcOffsetQ = offset;
    timezones::applyToClock();
    const auto& zone = timezones::table()[timezones::activeIndex()];
    EXPECT_EQ(strncmp(zone.name, "UTC", 3), 0);
    EXPECT_EQ(zone.stdOffsetQ, static_cast<int>(offset) - 48);
    EXPECT_EQ(SETTINGS.clockTimezone, 255);
    std::tm migrated{}, fixed{};
    ASSERT_TRUE(TimeUtils::getLocalDateTime(now, migrated));
    ASSERT_TRUE(TimeUtils::getLocalDateTime(now, offset, fixed));
    EXPECT_EQ(migrated.tm_yday, fixed.tm_yday);
    EXPECT_EQ(migrated.tm_hour, fixed.tm_hour);
    EXPECT_EQ(migrated.tm_min, fixed.tm_min);
  }

  SETTINGS.clockTimezone = zoneIndex("New York / Toronto");
  timezones::applyToClock();
  std::tm local{};
  ASSERT_TRUE(TimeUtils::getLocalDateTime(now, local));
  EXPECT_EQ(local.tm_hour, 23);
  EXPECT_EQ(local.tm_mday, 1);
  EXPECT_EQ(SETTINGS.clockUtcOffsetQ, 104);
}

TEST(TimeUtils, CityAndDstPolicyUnifyClockCalendarAndAnalytics) {
  ClockTestState state;
  SETTINGS.clockTimezone = zoneIndex("New York / Toronto");
  SETTINGS.clockUtcOffsetQ = 80;  // Must no longer override the selected city.
  struct Case {
    uint8_t dst;
    unsigned month;
    unsigned hour;
  };
  const Case cases[] = {
      {CrossPointSettings::CLOCK_DST_AUTO, 1, 22}, {CrossPointSettings::CLOCK_DST_AUTO, 7, 23},
      {CrossPointSettings::CLOCK_DST_ON, 1, 23},   {CrossPointSettings::CLOCK_DST_ON, 7, 23},
      {CrossPointSettings::CLOCK_DST_OFF, 1, 22},  {CrossPointSettings::CLOCK_DST_OFF, 7, 22},
  };
  for (const auto& test : cases) {
    SETTINGS.clockDst = test.dst;
    timezones::applyToClock();
    halClock.now = utcEpoch(2025, test.month, 2, 3, 15);
    std::tm local{};
    ASSERT_TRUE(TimeUtils::getLocalDateTime(halClock.now, local));
    EXPECT_EQ(local.tm_mday, 1);
    EXPECT_EQ(local.tm_hour, test.hour);
    EXPECT_EQ(TimeUtils::getLocalDayOrdinal(halClock.now), TimeUtils::getDayOrdinalForDate(2025, test.month, 1));
    const auto date = TimeUtils::formatDateParts(2025, test.month, 1);
    EXPECT_EQ(TimeUtils::formatDate(halClock.now), date);
    char time[9], dateTime[24];
    ASSERT_TRUE(TimeUtils::formatCurrentTime(time, sizeof(time), false));
    EXPECT_STREQ(time, test.hour == 23 ? "23:15" : "22:15");
    ASSERT_TRUE(TimeUtils::formatCurrentDateTime(dateTime, sizeof(dateTime), true));
    EXPECT_EQ(std::string(dateTime), date + (test.hour == 23 ? " 11:15 PM" : " 10:15 PM"));
    // The standby clock helper (standby_time::getNowHHMM) lived in the deleted
    // app suite; TimeUtils::getLocalDateTime above covers the same conversion.
    uint32_t epoch = 0;
    ASSERT_TRUE(TimeUtils::localDateTimeToUtcEpoch(2025, test.month, 1, test.hour, 15, epoch));
    EXPECT_EQ(epoch, halClock.now);
  }
}

TEST(TimeUtils, ManualDateRejectsDstGapAndRoundTripsRepeatedHour) {
  ClockTestState state;
  SETTINGS.clockTimezone = zoneIndex("New York / Toronto");
  timezones::applyToClock();
  uint32_t epoch = 123;
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2025, 3, 9, 2, 30, epoch));
  EXPECT_EQ(epoch, 123u);
  ASSERT_TRUE(TimeUtils::localDateTimeToUtcEpoch(2025, 11, 2, 1, 30, epoch));
  EXPECT_TRUE(epoch == utcEpoch(2025, 11, 2, 5, 30) || epoch == utcEpoch(2025, 11, 2, 6, 30));
  std::tm local{};
  ASSERT_TRUE(TimeUtils::getLocalDateTime(epoch, local));
  EXPECT_EQ(local.tm_hour, 1);
  EXPECT_EQ(local.tm_min, 30);
}

TEST(TimeUtils, InvalidAndTruncatedDateTimeDoesNotFormat) {
  ClockTestState state;
  char buffer[24];
  EXPECT_FALSE(TimeUtils::formatCurrentDateTime(buffer, sizeof(buffer), false));
  halClock.now = utcEpoch(2025, 1, 2, 3, 4);
  EXPECT_FALSE(TimeUtils::formatCurrentDateTime(buffer, 8, false));
  EXPECT_FALSE(TimeUtils::formatCurrentDateTime(nullptr, sizeof(buffer), false));
  EXPECT_FALSE(TimeUtils::formatCurrentTime(buffer, 5, false));
  EXPECT_FALSE(TimeUtils::formatCurrentTime(buffer, 8, true));
}

TEST(TimeUtils, InvalidCurrentClockDoesNotFormat) {
  ClockTestState state;
  char buffer[16];
  halClock.now = 0;
  EXPECT_FALSE(TimeUtils::isClockValid());
  EXPECT_EQ(TimeUtils::getCurrentValidTimestamp(), 0u);
  EXPECT_FALSE(TimeUtils::formatCurrentTime(buffer, sizeof(buffer), false));

  halClock.now = utcEpoch(2025, 1, 2, 3, 4);
  SETTINGS.clockTimezone = zoneIndex("Kathmandu");
  timezones::applyToClock();
  EXPECT_TRUE(TimeUtils::isClockValid());
  ASSERT_TRUE(TimeUtils::formatCurrentTime(buffer, sizeof(buffer), false));
  EXPECT_STREQ(buffer, "08:49");
}

// The standby clock / light-sleep helpers lived in the deleted app suite
// (src/activities/apps/standby/StandbyTime.cpp), so their coverage was removed
// along with the apps.
