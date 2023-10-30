//
//  EmptyPlugIns.cpp
//  EmptyPlugIns
//

#include <AudioUnitSDK/AUBase.h>
#include <AudioUnitSDK/AUEffectBase.h>
#include <AudioUnitSDK/MusicDeviceBase.h>

// -------------------------------------------------------------------------------------------------

class AUBase_Derived : public ausdk::AUBase {
	using Base = ausdk::AUBase;

public:
	explicit AUBase_Derived(AudioComponentInstance ci) : Base{ ci, 1, 1 } {}

	bool StreamFormatWritable(AudioUnitScope, AudioUnitElement) override { return true; }

	bool CanScheduleParameters() const override { return false; }
};

AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, AUBase_Derived)

// -------------------------------------------------------------------------------------------------

class AUEffectBase_Derived : public ausdk::AUEffectBase {
	using Base = ausdk::AUEffectBase;

public:
	explicit AUEffectBase_Derived(AudioComponentInstance ci) : Base{ ci, true } {}
};

AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, AUEffectBase_Derived)

// -------------------------------------------------------------------------------------------------

class MusicDeviceBase_Derived : public ausdk::MusicDeviceBase {
	using Base = ausdk::MusicDeviceBase;

public:
	explicit MusicDeviceBase_Derived(AudioComponentInstance ci) : Base{ ci, 0, 1 } {}

	bool StreamFormatWritable(AudioUnitScope, AudioUnitElement) override { return true; }

	bool CanScheduleParameters() const override { return false; }
};

AUSDK_COMPONENT_ENTRY(ausdk::AUMusicDeviceFactory, MusicDeviceBase_Derived)
