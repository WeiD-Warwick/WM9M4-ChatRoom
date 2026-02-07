#pragma once
#include <fmod.hpp>
#include <fmod_errors.h>
#include <iostream>
#include <string>

class AudioPlayer {
private:
    FMOD::System* system = nullptr;
    FMOD::Sound* alertSound = nullptr;
    FMOD::Sound* joinSound = nullptr;
    FMOD::Channel* channel = nullptr;

public:
    AudioPlayer() {}

    ~AudioPlayer() { stop(); }


    bool init() {
        FMOD_RESULT result;

        result = FMOD::System_Create(&system);
        if (result != FMOD_OK) return false;
        result = system->init(32, FMOD_INIT_NORMAL, nullptr);
        if (result != FMOD_OK) return false;
        system->createSound("audio/Alert.wav", FMOD_CREATESAMPLE, nullptr, &alertSound);
        system->createSound("audio/Join.mp3", FMOD_CREATESAMPLE, nullptr, &joinSound);
        return true;
    }

    void playAlert() {
        if (system && alertSound) {
            system->playSound(alertSound, nullptr, false, &channel);
        }
    }

    void playJoin() {
        if (system && joinSound) {
            system->playSound(joinSound, nullptr, false, &channel);
        }
    }

    void update() {
        if (system) {
            system->update();
        }
    }

    void stop() {
        if (alertSound) alertSound->release();
        if (joinSound) joinSound->release();
        if (system) {
            system->close();
            system->release();
        }
    }
};