#include <TimeLib.h> 
#include <SmallRTC.h>

RTC_DATA_ATTR int MyTimeDriftWatchFace; // Watchface ID #.
RTC_DATA_ATTR int32_t lastTimeDrift = 0;
const int max_samples = 144; // Store drift samples for the last 24 hours
RTC_DATA_ATTR int32_t driftSamples[max_samples] = {0}; // Store drift samples for the last 24 hours
RTC_DATA_ATTR int sampleCount = 0;             // Number of samples stored
RTC_DATA_ATTR time_t lastNTPCheck = 0;         // Last time NTP was checked
RTC_DATA_ATTR int32_t testSyncDrift = 0; // Store test sync result temporarily
RTC_DATA_ATTR int32_t minute_step = 0;

class GSRWatchFaceTimeDrift : public WatchyGSR
{
public:
    GSRWatchFaceTimeDrift() : WatchyGSR() { initAddOn(this); } // *** DO NOT EDIT THE CONTENTS OF THIS FUNCTION ***

    void RegisterWatchFaces()
    { // Add WatchStyles here (this is done pre-boot, post init)
        MyTimeDriftWatchFace = AddWatchStyle("TimeDrift", this);
    };

    String InsertNTPServer()
    {
        return "pool.ntp.org";
    }

    // bool InsertNeedAwake(bool GoingAsleep) { return true; }

    void InsertInitWatchStyle(uint8_t StyleID)
    {
        if (StyleID == MyTimeDriftWatchFace)
        {
        }
    };

    void ResetWatchFace()
    {
        lastTimeDrift = 0;
        for (int i = 0; i < max_samples; i++)
        {
            driftSamples[i] = 0;
        }
        sampleCount = 0;
        lastNTPCheck = 0;
        testSyncDrift = 0;
        minute_step = 0;
    }

    void InsertDrawWatchStyle(uint8_t StyleID)
    {
        uint8_t X, Y;
        uint16_t A;
        if (StyleID == MyTimeDriftWatchFace)
        {
            // Display drift information
            display.setFont(nullptr);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(0, 20); // Start from the top

            tmElements_t readTime;
            SRTC.read(readTime);
            time_t currentTime = makeTime(readTime);

            // Display current time
            display.print("Now: ");
            display.println(currentTime);

            display.print("Last Check Time: ");
            display.println(lastNTPCheck);

            display.print("Last Drift Offset: ");
            display.println(driftSamples[0]);

            int32_t totalDrift = 0;
            for (int i = 0; i < sampleCount; i++)
            {
                totalDrift += driftSamples[i];
            }
            display.print("Avg Drift (24hr): ");
            display.println(sampleCount > 0 ? totalDrift / sampleCount : 0);

            int32_t lastHourDrift = 0;
            int hourSamples = sampleCount < 6 ? sampleCount : 6;
            for (int i = 0; i < hourSamples; i++)
            {
                lastHourDrift += driftSamples[i];
            }
            display.print("Avg Drift (1hr): ");
            display.println(hourSamples > 0 ? lastHourDrift / hourSamples : 0);

            display.print("Test Sync Drift: ");
            display.println(testSyncDrift);
        }
    };

    void InsertOnMinute()
    {
        bool skip = minute_step % 10 != 0;
        minute_step++;
        if (skip)
        {
            return;
        }

        log_e("Checking time drift...");
        tmElements_t queryNow;
        if (QueryNTPTime(queryNow))
        {
            log_e("Time drift check successful.");
            tmElements_t readTime;
            SRTC.read(readTime);
            time_t currentTime = makeTime(readTime);
            time_t queryNowTime = makeTime(queryNow);
            int32_t drift = currentTime - queryNowTime;
            lastTimeDrift += drift;
            for (int i = max_samples -1; i > 0; i--)
            {
                driftSamples[i] = driftSamples[i - 1];
            }
            driftSamples[0] = drift;
            if (sampleCount < max_samples)
            {
                sampleCount++;
            }
            log_e("Time Drift: %d", drift);

            lastNTPCheck = queryNowTime;
            UpdateScreen();
        }
        else
        {
            log_e("Time drift check failed.");
        }
    }

    bool InsertHandlePressed(uint8_t SwitchNumber, bool &Haptic, bool &Refresh)
    {
        if (SwitchNumber == 2)
        {
            tmElements_t queryNow;
            if (QueryNTPTime(queryNow))
            {
                // Force NTP sync and update internal clock
                SRTC.set(queryNow);
                Haptic = true;
                Refresh = true;
                log_e("Internal clock updated with NTP time: %ld", queryNow);
                return true;
            }
            else
            {
                log_e("Failed to update internal clock with NTP time.");
            }
        }
        else if (SwitchNumber == 3)
        {
            ResetWatchFace();
            Haptic = true;
            Refresh = true;
            log_e("Watchface reset.");
            return true;
        }
        else if (SwitchNumber == 4)
        {
            tmElements_t queryNow;
            // Get the current time from the NTP server
            if (QueryNTPTime(queryNow))
            {
                tmElements_t readTime;
                SRTC.read(readTime);
                time_t currentTime = makeTime(readTime);
                time_t queryNowTime = makeTime(queryNow);
                int32_t drift = currentTime - queryNowTime;
                testSyncDrift = drift;
                log_e("Test Sync Drift: %d", drift);

                Haptic = true;
                Refresh = true;
                return true;
            }
        }

        return false;
    }

private:
    bool QueryNTPTime(tmElements_t &result)
    {
        log_e("Starting NTP time query...");
        if (WiFi.status() != WL_CONNECTED)
        {
            log_e("WiFi not connected. Enabling WiFi...");
            WiFi.begin();
            unsigned long startWiFi = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startWiFi < 10000)
            {
                delay(500);
                log_e("Connecting to WiFi...");
            }
            if (WiFi.status() != WL_CONNECTED)
            {
                log_e("Failed to connect to WiFi.");
                return false;
            }
        }

        SNTP.Begin(InsertNTPServer());

        unsigned long start = millis();
        bool success = false;
        while (millis() - start < 5000)
        {
            if (SNTP.Query())
            {
                result = SNTP.tmResults;
                success = true;
            }
        }

        if (success)
        {
            log_e("NTP time query successful. Time: %ld", result);
        }
        else
        {
            log_e("NTP time query failed.");
        }

        SNTP.End();

        return success;
    }
};

GSRWatchFaceTimeDrift TimeDrift;
