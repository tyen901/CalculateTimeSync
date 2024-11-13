#include <TimeLib.h> 
#include <SmallRTC.h>

RTC_DATA_ATTR int MyAutoDriftWatchFace; // Watchface ID #.
RTC_DATA_ATTR int ad_minuteStep = 0;             // Number of samples stored
RTC_DATA_ATTR int32_t ad_totalDrift = 0; // Total drift accumulated over 10 minutes
RTC_DATA_ATTR bool ad_autosync = false; // Flag to control automatic syncing
RTC_DATA_ATTR int32_t ad_driftSamples[6] = {0}; // Store drift samples
RTC_DATA_ATTR int ad_driftSampleActiveIndex = 0; // Index for storing drift samples

class GSRWatchFaceAutoDrift : public WatchyGSR
{
public:
    GSRWatchFaceAutoDrift() : WatchyGSR() { initAddOn(this); } // *** DO NOT EDIT THE CONTENTS OF THIS FUNCTION ***

    void RegisterWatchFaces()
    { // Add WatchStyles here (this is done pre-boot, post init)
        MyAutoDriftWatchFace = AddWatchStyle("Auto Drift", this);
    };

    String InsertNTPServer()
    {
        return "pool.ntp.org";
    }

    // bool InsertNeedAwake(bool GoingAsleep) { return true; }

    void InsertInitWatchStyle(uint8_t StyleID)
    {
        if (StyleID == MyAutoDriftWatchFace)
        {
        }
    };

    void ResetWatchFace()
    {
        ad_autosync = false;
        ad_minuteStep = 0;
        ad_totalDrift = 0;
        for (int i = 0; i < 6; i++) {
            ad_driftSamples[i] = 0;
        }
        ad_driftSampleActiveIndex = 0;
    }

    void InsertDrawWatchStyle(uint8_t StyleID)
    {
        uint8_t X, Y;
        uint16_t A;
        if (StyleID == MyAutoDriftWatchFace)
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

            // Display whether automatic syncing is enabled
            display.print("Auto Sync: ");
            display.println(ad_autosync ? "Enabled" : "Disabled");

            // Display current calculated drift over 10 minutes
            display.print("Current Drift: ");
            display.println(ad_totalDrift / (ad_minuteStep > 0 ? ad_minuteStep : 1));

            // Display drift samples
            display.print("Drift Samples: ");
            for (int i = 0; i < 6; i++) {
                display.print(ad_driftSamples[i]);
                if (i < 5) {
                    display.print(", ");
                }
            }
            display.println();
        }
    };

    void InsertOnMinute()
    {
        if (!ad_autosync) {
            return;
        }

        log_e("Checking time drift...");
        tmElements_t readTime, ntpTime;
        if (!QueryNTPTime(ntpTime)) {
            log_e("Failed to query NTP time.");
            return;
        }

        SRTC.read(readTime);
        time_t currentTime = makeTime(readTime);
        time_t ntpCurrentTime = makeTime(ntpTime);

        // If readTime is less than 10000, the RTC is not set
        if (currentTime < 10000) {
            log_e("RTC not set. Skipping drift calculation.");
            SRTC.set(ntpTime);
            return;
        }

        if (ad_minuteStep == 0) {
            SRTC.beginDrift(readTime, true); // Start drift calculation
        } else if (ad_minuteStep < 10) {
            SRTC.endDrift(ntpTime, true); // End drift calculation and apply correction using ntpTime
            ad_totalDrift += currentTime - ntpCurrentTime; // Accumulate drift for debugging
            SRTC.beginDrift(readTime, true); // Restart drift calculation
        } else {
            int32_t avgDrift = ad_totalDrift / 10; // Calculate average drift
            log_e("Average Drift over 10 minutes: %d", avgDrift);

            // Store the average drift in the drift samples
            ad_driftSamples[ad_driftSampleActiveIndex] = avgDrift;
            ad_driftSampleActiveIndex = (ad_driftSampleActiveIndex + 1) % 6; // Update the index for storing drift samples

            ad_totalDrift = 0; // Reset total drift
            ad_minuteStep = 0; // Reset sample count
            SRTC.beginDrift(readTime, true); // Restart drift calculation
        }

        ad_minuteStep++;
        lastNTPCheck = ntpCurrentTime;
        UpdateScreen();
    }

    bool InsertHandlePressed(uint8_t SwitchNumber, bool &Haptic, bool &Refresh)
    {
        if (SwitchNumber == 2)
        {
            ad_autosync = !ad_autosync; // Toggle automatic syncing
            Haptic = true;
            Refresh = true;
            log_e("Automatic syncing %s", ad_autosync ? "enabled" : "disabled");
            return true;
        }
        else if (SwitchNumber == 3)
        {
            ResetWatchFace();

            log_e("Setting current time");
            tmElements_t ntpTime;
            if (!QueryNTPTime(ntpTime)) {
                log_e("Failed to query NTP time.");
                return false;
            }

            SRTC.set(ntpTime);

            Haptic = true;
            Refresh = true;
            log_e("Watchface reset.");
            return true;
        }
        else if (SwitchNumber == 4)
        {

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

GSRWatchFaceAutoDrift AutoDrift;
