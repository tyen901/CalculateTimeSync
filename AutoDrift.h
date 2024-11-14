#include <TimeLib.h>
#include <SmallRTC.h>

RTC_DATA_ATTR int MyAutoDriftWatchFace;             // Watchface ID #.
RTC_DATA_ATTR bool ad_autosync = false;             // Flag to control automatic syncing

RTC_DATA_ATTR int ad_driftSampleStep = 0;           // Number of samples stored
RTC_DATA_ATTR int32_t ad_internalDriftSamples[10] = {0}; // Store internal drift samples
RTC_DATA_ATTR int32_t ad_chipDriftSamples[10] = {0};     // Store chip drift samples

RTC_DATA_ATTR int ad_currentSetIndex = 0;                    // Index of the active drift set
RTC_DATA_ATTR int32_t ad_internalDriftSetSamples[10] = {0}; // Store internal drift samples
RTC_DATA_ATTR int32_t ad_chipDriftSetSamples[10] = {0};     // Store chip drift samples

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

    bool InsertNeedAwake(bool GoingAsleep) { return true; }

    void InsertInitWatchStyle(uint8_t StyleID)
    {
        if (StyleID == MyAutoDriftWatchFace)
        {
        }
    };

    void FullReset()
    {
        ad_autosync = false;
        ad_driftSampleStep = 0;
        ad_currentSetIndex = 0;

        for (int i = 0; i < 10; i++)
        {
            ad_internalDriftSamples[i] = 0;
            ad_chipDriftSamples[i] = 0;
            ad_internalDriftSetSamples[i] = 0;
            ad_chipDriftSetSamples[i] = 0;
        }
    }

    void DisplaySamples(
        String inTitle,
        int32_t* inSamples,
        int32_t* inSetSamples,
        int inSampleCount
        )
    {
        // Header
        display.print(inTitle);
        display.println();

        display.print("Average Drift: ");
        display.println();

        int32_t totalDrift = 0;
        for (int i = 0; i < 10; i++)
        {
            totalDrift += inSamples[i];
        }
        display.println(totalDrift / (inSampleCount > 0 ? inSampleCount : 1));

        display.print("Samples " + inTitle);
        display.println();
        for (int i = 0; i < 10; i++)
        {
            if (inSamples[i] != 0)
            {
                display.print(inSamples[i]);
            }
            else
            {
                display.print("-");
            }
            if (i < 9)
            {
                display.print(", ");
            }
        }
        display.println();

        display.print("Set Samples " + inTitle);
        display.println();
        for (int i = 0; i < 10; i++)
        {
            if (inSetSamples[i] != 0)
            {
                display.print(inSetSamples[i]);
            }
            else
            {
                display.print("-");
            }
            if (i < 9)
            {
                display.print(", ");
            }
        }
        display.println();
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

            display.println();

            DisplaySamples(
                "Internal",
                ad_internalDriftSamples,
                ad_internalDriftSetSamples,
                ad_driftSampleStep
            );

            display.println();

            DisplaySamples(
                "Chip",
                ad_chipDriftSamples,
                ad_chipDriftSetSamples,
                ad_driftSampleStep
            );
        }
    };

    void InsertOnMinute()
    {
        if (!ad_autosync)
        {
            return;
        }

        log_e("Checking time drift...");
        tmElements_t internalTime, chipTime, ntpTime;
        if (!QueryNTPTime(ntpTime))
        {
            log_e("Failed to query NTP time.");
            return;
        }

        SRTC.read(internalTime, true);
        SRTC.read(chipTime, false);
        time_t ntpCurrentTime = makeTime(ntpTime);
        time_t currentInternalTime = makeTime(internalTime);
        time_t currentChipTime = makeTime(chipTime);

        // If internalTime is less than 10000, the RTC is not set
        if (currentInternalTime < 10000)
        {
            SRTC.set(ntpTime, false, false);
            SRTC.set(ntpTime, false, true);
            return;
        }

        int32_t internalDrift = currentInternalTime - ntpCurrentTime;
        int32_t chipDrift = currentChipTime - ntpCurrentTime;

        ad_internalDriftSamples[ad_driftSampleStep] = internalDrift; // Store internal drift for each minute
        ad_chipDriftSamples[ad_driftSampleStep] = chipDrift;         // Store chip drift for each minute

        if (ad_driftSampleStep == 0)
        {
            // Start drift calculation
            SRTC.beginDrift(internalTime, true); 
        }
        else if (ad_driftSampleStep < 10)
        {
            // Continue collecting drift samples
        }
        else
        {
            // End drift calculation and apply correction using ntpTime
            SRTC.endDrift(ntpTime, true);
            SRTC.endDrift(ntpTime, false);

            int32_t totalInternalDrift = 0;
            int32_t totalChipDrift = 0;
            for (int i = 0; i < 10; i++)
            {
                totalInternalDrift += ad_internalDriftSamples[i];
                totalChipDrift += ad_chipDriftSamples[i];
            }
            int32_t avgInternalDrift = totalInternalDrift / 10; // Calculate average internal drift
            int32_t avgChipDrift = totalChipDrift / 10;         // Calculate average chip drift

            // Clear the drift samples
            for (int i = 0; i < 10; i++)
            {
                ad_internalDriftSamples[i] = 0;
                ad_chipDriftSamples[i] = 0;
            }
            ad_driftSampleStep = 0;

            // Store the average internal and chip drifts in the sets
            ad_internalDriftSetSamples[ad_currentSetIndex] = avgInternalDrift;
            ad_chipDriftSetSamples[ad_currentSetIndex] = avgChipDrift;

            // Update the index of the active drift set
            ad_currentSetIndex++;

            // If the time goes beyond 10, we've done 10 hours of drift calculation and can stop
            if (ad_currentSetIndex == 10)
            {
                ad_autosync = false;
                log_e("Automatic syncing disabled.");
                return;
            }
        }

        ad_driftSampleStep++;

        UpdateScreen();
    }

    bool InsertHandlePressed(uint8_t SwitchNumber, bool &Haptic, bool &Refresh)
    {
        if (SwitchNumber == 2)
        {
            FullReset();
            Haptic = true;
            Refresh = true;
            log_e("Watchface reset.");
            return true;
        }
        else if (SwitchNumber == 3)
        {
            ad_autosync = !ad_autosync;
            Haptic = true;
            Refresh = true;
            log_e("Automatic syncing %s", ad_autosync ? "enabled" : "disabled");
            return true;
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
            log_e("NTP time query successful.");
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
