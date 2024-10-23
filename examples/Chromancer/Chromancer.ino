/*
   Original Source: https://github.com/ZackFreedman/Chromance
   Chromance wall hexagon source (emotion controlled w/ EmotiBit)
   Partially cribbed from the DotStar example
   I smooshed in the ESP32 BasicOTA sketch, too

   (C) Voidstar Lab 2021
*/

#include "mapping.h"
#include "net.h"
#include "ripple.h"
#include <FastLED.h>
#include "data.h"
#include "detail.h"
#include "screenmap.h"
#include "math_macros.h"
#include "third_party/arduinojson/json.h"
#include "ui.h"

#include "screenmap.json.h"

// Strips are different lengths because I am a dumb
constexpr int lengths[] = {
  154, // Black strip?
  168, // Green strip?
  84,  // Red strip
  154  // Blue strip?
};

enum {
    BlackStrip = 0,
    GreenStrip = 1,
    RedStrip = 2,
    BlueStrip = 3,
};



/*
Adafruit_DotStar strip0(lengths[0], 15, 2, DOTSTAR_BRG);
Adafruit_DotStar strip1(lengths[1], 0, 4, DOTSTAR_BRG);
Adafruit_DotStar strip2(lengths[2], 16, 17, DOTSTAR_BRG);
Adafruit_DotStar strip3(lengths[3], 5, 18, DOTSTAR_BRG);


Adafruit_DotStar strips[4] = {strip0, strip1, strip2, strip3};
*/

// #define NUM_STRIPS 4

/*
GPIO: 16 - Start: 0 - Length: 84
GPIO: 15 - Start: 84 - Length: 154
GPIO: 0 - Start: 238 - Length: 168
GPIO: 5 - Start: 406 - Length: 154
*/

//const int TOTAL_LEDS = lengths[0] + lengths[1] + lengths[2] + lengths[3];
//CRGB led_all

// non emscripten uses separate arrays for each strip. Eventually emscripten
// should support this as well but right now we don't
CRGB leds0[lengths[0]] = {};
CRGB leds1[lengths[1]] = {};
CRGB leds2[lengths[2]] = {}; // Red 
CRGB leds3[lengths[3]] = {};
CRGB *leds[] = {leds0, leds1, leds2, leds3};


byte ledColors[40][14][3]; // LED buffer - each ripple writes to this, then we
                           // write this to the strips
//float decay = 0.97; // Multiply all LED's by this amount each tick to create
                    // fancy fading tails

Slider decay("decay", .97f, .8, 1.0, .01);

// These ripples are endlessly reused so we don't need to do any memory
// management
#define numberOfRipples 30
Ripple ripples[numberOfRipples] = {
    Ripple(0),  Ripple(1),  Ripple(2),  Ripple(3),  Ripple(4),  Ripple(5),
    Ripple(6),  Ripple(7),  Ripple(8),  Ripple(9),  Ripple(10), Ripple(11),
    Ripple(12), Ripple(13), Ripple(14), Ripple(15), Ripple(16), Ripple(17),
    Ripple(18), Ripple(19), Ripple(20), Ripple(21), Ripple(22), Ripple(23),
    Ripple(24), Ripple(25), Ripple(26), Ripple(27), Ripple(28), Ripple(29),
};

// Biometric detection and interpretation
// IR (heartbeat) is used to fire outward ripples
float lastIrReading;    // When our heart pumps, reflected IR drops sharply
float highestIrReading; // These vars let us detect this drop
unsigned long
    lastHeartbeat; // Track last heartbeat so we can detect noise/disconnections
#define heartbeatLockout                                                       \
    500 // Heartbeats that happen within this many milliseconds are ignored
#define heartbeatDelta 300 // Drop in reflected IR that constitutes a heartbeat

// Heartbeat color ripples are proportional to skin temperature
#define lowTemperature 33.0  // Resting temperature
#define highTemperature 37.0 // Really fired up
float lastKnownTemperature =
    (lowTemperature + highTemperature) /
    2.0; // Carries skin temperature from temperature callback to IR callback

// EDA code was too unreliable and was cut.
// TODO: Rebuild EDA code

// Gyroscope is used to reject data if you're moving too much
#define gyroAlpha 0.9 // Exponential smoothing constant
#define gyroThreshold                                                          \
    300 // Minimum angular velocity total (X+Y+Z) that disqualifies readings
float gyroX, gyroY, gyroZ;

// If you don't have an EmotiBit or don't feel like wearing it, that's OK
// We'll fire automatic pulses
#define randomPulsesEnabled true // Fire random rainbow pulses from random nodes
#define cubePulsesEnabled true   // Draw cubes at random nodes
#define starburstPulsesEnabled true      // Draw starbursts
#define simulatedBiometricsEnabled false // Simulate heartbeat and EDA ripples

#define autoPulseTimeout                                                       \
    5000 // If no heartbeat is received in this many ms, begin firing
         // random/simulated pulses
#define randomPulseTime 2000 // Fire a random pulse every (this many) ms
unsigned long lastRandomPulse;
byte lastAutoPulseNode = 255;

byte numberOfAutoPulseTypes =
    randomPulsesEnabled + cubePulsesEnabled + starburstPulsesEnabled;
byte currentAutoPulseType = 255;
#define autoPulseChangeTime 30000
unsigned long lastAutoPulseChange;

#define simulatedHeartbeatBaseTime                                             \
    600 // Fire a simulated heartbeat pulse after at least this many ms
#define simulatedHeartbeatVariance                                             \
    200                           // Add random jitter to simulated heartbeat
#define simulatedEdaBaseTime 1000 // Same, but for inward EDA pulses
#define simulatedEdaVariance 10000
unsigned long nextSimulatedHeartbeat;
unsigned long nextSimulatedEda;


void make_map(int xstep, int ystep, int num, std::vector<pair_xy_float>* _map) {
    float x = 0;
    float y = 0;
    std::vector<pair_xy_float>& map = *_map;
    for (int16_t i = 0; i < num; i++) {
        map.push_back(pair_xy_float{x, y});
        x += xstep;
        y += ystep;
    }
}



ScreenMap make_screen_map(int xstep, int ystep, int num) {
    std::vector<pair_xy_float> map;
    make_map(xstep, ystep, num, &map);
    return ScreenMap(map.data(), map.size());
}

void setup() {
    Serial.begin(115200);

    Serial.println("*** LET'S GOOOOO ***");

    Serial.println("JSON SCREENMAP");
    Serial.println(JSON_SCREEN_MAP);

    ArduinoJson::JsonDocument doc;
    // ingest the JSON_SCREEN_MAP
    ArduinoJson::deserializeJson(doc, JSON_SCREEN_MAP);

    
    auto map = doc["map"];
    const char* segments[] = {"red_segment", "back_segment", "green_segment", "blue_segment"};
    ScreenMap screenmaps[4];

    for (int i = 0; i < 4; i++) {
        auto segment = map[segments[i]];
        auto x = segment["x"];
        auto y = segment["y"];
        
        std::vector<pair_xy_float> segment_map;
        for (int j = 0; j < x.size(); j++) {
            segment_map.push_back(pair_xy_float{x[j], y[j]});
        }
        
        screenmaps[i] = ScreenMap(segment_map.data(), segment_map.size());
    }

    ScreenMap& red_screenmap = screenmaps[0];
    ScreenMap& black_screenmap = screenmaps[1];
    ScreenMap& green_screenmap = screenmaps[2];
    ScreenMap& blue_screenmap = screenmaps[3];


    const char* colors[] = {"RED", "BLACK", "GREEN", "BLUE"};
    for (int i = 0; i < 4; i++) {
        Serial.print("\n");
        Serial.print(colors[i]);
        Serial.println(" SCREENMAP");
        Serial.println(screenmaps[i].getLength());
    }
    Serial.println("");

    CRGB* red_leds = leds[RedStrip];
    CRGB* black_leds = leds[BlackStrip];
    CRGB* green_leds = leds[GreenStrip];
    CRGB* blue_leds = leds[BlueStrip];

    CLEDController* controllers[4];
    // Initialize FastLED strips
    controllers[RedStrip] = &FastLED.addLeds<WS2812, 1>(red_leds, lengths[RedStrip]);
    controllers[BlackStrip] = &FastLED.addLeds<WS2812, 2>(black_leds, lengths[BlackStrip]);
    controllers[GreenStrip] = &FastLED.addLeds<WS2812, 3>(green_leds, lengths[GreenStrip]);
    controllers[BlueStrip] = &FastLED.addLeds<WS2812, 4>(blue_leds, lengths[BlueStrip]);
    controllers[RedStrip]->setCanvasUi(red_screenmap);
    controllers[BlackStrip]->setCanvasUi(black_screenmap);
    controllers[GreenStrip]->setCanvasUi(green_screenmap);
    controllers[BlueStrip]->setCanvasUi(blue_screenmap);


    FastLED.show();
    net_init();
}


void loop() {
    unsigned long benchmark = millis();
    net_loop();

    // Fade all dots to create trails
    for (int strip = 0; strip < 40; strip++) {
        for (int led = 0; led < 14; led++) {
            for (int i = 0; i < 3; i++) {
                ledColors[strip][led][i] *= decay.value();
            }
        }
    }

    for (int i = 0; i < numberOfRipples; i++) {
        ripples[i].advance(ledColors);
    }

    for (int segment = 0; segment < 40; segment++) {
        for (int fromBottom = 0; fromBottom < 14; fromBottom++) {
            int strip = ledAssignments[segment][0];
            int led = round(fmap(fromBottom, 0, 13, ledAssignments[segment][2],
                                 ledAssignments[segment][1]));
            leds[strip][led] = CRGB(ledColors[segment][fromBottom][0],
                                    ledColors[segment][fromBottom][1],
                                    ledColors[segment][fromBottom][2]);
        }
    }

    FastLED.show();


    if (millis() - lastHeartbeat >= autoPulseTimeout) {
        // When biometric data is unavailable, visualize at random
        if (numberOfAutoPulseTypes &&
            millis() - lastRandomPulse >= randomPulseTime) {
            unsigned int baseColor = random(0xFFFF);

            if (currentAutoPulseType == 255 ||
                (numberOfAutoPulseTypes > 1 &&
                 millis() - lastAutoPulseChange >= autoPulseChangeTime)) {
                byte possiblePulse = 255;
                while (true) {
                    possiblePulse = random(3);

                    if (possiblePulse == currentAutoPulseType)
                        continue;

                    switch (possiblePulse) {
                    case 0:
                        if (!randomPulsesEnabled)
                            continue;
                        break;

                    case 1:
                        if (!cubePulsesEnabled)
                            continue;
                        break;

                    case 2:
                        if (!starburstPulsesEnabled)
                            continue;
                        break;

                    default:
                        continue;
                    }

                    currentAutoPulseType = possiblePulse;
                    lastAutoPulseChange = millis();
                    break;
                }
            }

            switch (currentAutoPulseType) {
            case 0: {
                int node = 0;
                bool foundStartingNode = false;
                while (!foundStartingNode) {
                    node = random(25);
                    foundStartingNode = true;
                    for (int i = 0; i < numberOfBorderNodes; i++) {
                        // Don't fire a pulse on one of the outer nodes - it
                        // looks boring
                        if (node == borderNodes[i])
                            foundStartingNode = false;
                    }

                    if (node == lastAutoPulseNode)
                        foundStartingNode = false;
                }

                lastAutoPulseNode = node;

                for (int i = 0; i < 6; i++) {
                    if (nodeConnections[node][i] >= 0) {
                        for (int j = 0; j < numberOfRipples; j++) {
                            if (ripples[j].state == dead) {
                                ripples[j].start(
                                    node, i,
                                    //                      strip0.ColorHSV(baseColor
                                    //                      + (0xFFFF / 6) * i,
                                    //                      255, 255),
                                    Adafruit_DotStar_ColorHSV(baseColor, 255,
                                                              255),
                                    float(random(100)) / 100.0 * .2 + .5, 3000,
                                    1);

                                break;
                            }
                        }
                    }
                }
                break;
            }

            case 1: {
                int node = cubeNodes[random(numberOfCubeNodes)];

                while (node == lastAutoPulseNode)
                    node = cubeNodes[random(numberOfCubeNodes)];

                lastAutoPulseNode = node;

                byte behavior = random(2) ? alwaysTurnsLeft : alwaysTurnsRight;

                for (int i = 0; i < 6; i++) {
                    if (nodeConnections[node][i] >= 0) {
                        for (int j = 0; j < numberOfRipples; j++) {
                            if (ripples[j].state == dead) {
                                ripples[j].start(
                                    node, i,
                                    //                      strip0.ColorHSV(baseColor
                                    //                      + (0xFFFF / 6) * i,
                                    //                      255, 255),
                                    Adafruit_DotStar_ColorHSV(baseColor, 255,
                                                              255),
                                    .5, 2000, behavior);

                                break;
                            }
                        }
                    }
                }
                break;
            }

            case 2: {
                byte behavior = random(2) ? alwaysTurnsLeft : alwaysTurnsRight;

                lastAutoPulseNode = starburstNode;

                for (int i = 0; i < 6; i++) {
                    for (int j = 0; j < numberOfRipples; j++) {
                        if (ripples[j].state == dead) {
                            ripples[j].start(
                                starburstNode, i,
                                Adafruit_DotStar_ColorHSV(
                                    baseColor + (0xFFFF / 6) * i, 255, 255),
                                .65, 1500, behavior);

                            break;
                        }
                    }
                }
                break;
            }

            default:
                break;
            }
            lastRandomPulse = millis();
        }

        if (simulatedBiometricsEnabled) {
            // Simulated heartbeat
            if (millis() >= nextSimulatedHeartbeat) {
                for (int i = 0; i < 6; i++) {
                    for (int j = 0; j < numberOfRipples; j++) {
                        if (ripples[j].state == dead) {
                            ripples[j].start(
                                15, i, 0xEE1111,
                                float(random(100)) / 100.0 * .1 + .4, 1000, 0);

                            break;
                        }
                    }
                }

                nextSimulatedHeartbeat = millis() + simulatedHeartbeatBaseTime +
                                         random(simulatedHeartbeatVariance);
            }

            // Simulated EDA ripples
            if (millis() >= nextSimulatedEda) {
                for (int i = 0; i < 10; i++) {
                    for (int j = 0; j < numberOfRipples; j++) {
                        if (ripples[j].state == dead) {
                            byte targetNode =
                                borderNodes[random(numberOfBorderNodes)];
                            byte direction = 255;

                            while (direction == 255) {
                                direction = random(6);
                                if (nodeConnections[targetNode][direction] < 0)
                                    direction = 255;
                            }

                            ripples[j].start(
                                targetNode, direction, 0x1111EE,
                                float(random(100)) / 100.0 * .5 + 2, 300, 2);

                            break;
                        }
                    }
                }

                nextSimulatedEda = millis() + simulatedEdaBaseTime +
                                   random(simulatedEdaVariance);
            }
        }
    }

    //  Serial.print("Benchmark: ");
    //  Serial.println(millis() - benchmark);
}
